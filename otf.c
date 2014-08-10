#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "trfn.h"

#define NGLYPHS		(1 << 14)
#define GNLEN		(64)
#define BUFLEN		(1 << 23)
#define NGRPS		2048
#define MAX(a, b)	((a) < (b) ? (b) : (a))

#define U32(buf, off)		(htonl(*(u32 *) ((buf) + (off))))
#define U16(buf, off)		(htons(*(u16 *) ((buf) + (off))))
#define U8(buf, off)		(*(u8 *) ((buf) + (off)))
#define S16(buf, off)		((s16) htons(*(u16 *) ((buf) + (off))))
#define S32(buf, off)		((s32) htonl(*(u32 *) ((buf) + (off))))

#define OTFLEN		12	/* otf header length */
#define OTFRECLEN	16	/* otf header record length */
#define CMAPLEN		4	/* cmap header length */
#define CMAPRECLEN	8	/* cmap record length */
#define CMAP4LEN	8	/* format 4 cmap subtable header length */
#define GCTXLEN		16	/* number of context backtrack coverage arrays */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef int s32;
typedef short s16;

static char glyph_name[NGLYPHS][GNLEN];
static int glyph_code[NGLYPHS];
static int glyph_bbox[NGLYPHS][4];
static int glyph_wid[NGLYPHS];
static int glyph_n;
static int upm;			/* units per em */
static int res;			/* device resolution */
static int kmin;		/* minimum kerning value */
static int warn;

static char *macset[];

static int owid(int w)
{
	return (w < 0 ? w * 1000 - upm / 2 : w * 1000 + upm / 2) / upm;
}

static int uwid(int w)
{
	int d = 7200 / res;
	return (w < 0 ? owid(w) - d / 2 : owid(w) + d / 2) / d;
}

/* report unsupported otf tables */
static void otf_unsupported(char *sub, int type, int fmt)
{
	if (warn) {
		fprintf(stderr, "neatmkfn: unsupported %s lookup %d", sub, type);
		if (fmt > 0)
			fprintf(stderr, " format %d", fmt);
		fprintf(stderr, "\n");
	}
}

/* find the otf table with the given name */
static void *otf_table(void *otf, char *name)
{
	void *recs = otf + OTFLEN;	/* otf table records */
	void *rec;			/* beginning of a table record */
	int nrecs = U16(otf, 4);
	int i;
	for (i = 0; i < nrecs; i++) {
		rec = recs + i * OTFRECLEN;
		if (!strncmp(rec, name, 4))
			return otf + U32(rec, 8);
	}
	return NULL;
}

/* obtain postscript font name from name table */
static void otf_name(void *otf, void *tab)
{
	char name[256];
	void *str = tab + U16(tab, 4);		/* storage area */
	int n = U16(tab, 2);			/* number of name records */
	int i;
	for (i = 0; i < n; i++) {
		void *rec = tab + 6 + 12 * i;
		int pid = U16(rec, 0);		/* platform id */
		int eid = U16(rec, 2);		/* encoding id */
		int lid = U16(rec, 4);		/* language id */
		int nid = U16(rec, 6);		/* name id */
		int len = U16(rec, 8);		/* string length */
		int off = U16(rec, 10);		/* string offset  */
		if (pid == 1 && eid == 0 && lid == 0 && nid == 6) {
			memcpy(name, str + off, len);
			name[len] = '\0';
			trfn_psfont(name);
		}
	}
}

/* parse otf cmap format 4 subtable */
static void otf_cmap4(void *otf, void *cmap4)
{
	int nsegs;
	void *ends, *begs, *deltas, *offsets;
	int beg, end, delta, offset;
	int i, j;
	nsegs = U16(cmap4, 6) / 2;
	ends = cmap4 + 14;
	begs = ends + 2 * nsegs + 2;
	deltas = begs + 2 * nsegs;
	offsets = deltas + 2 * nsegs;
	for (i = 0; i < nsegs; i++) {
		beg = U16(begs, 2 * i);
		end = U16(ends, 2 * i);
		delta = U16(deltas, 2 * i);
		offset = U16(offsets, 2 * i);
		if (offset) {
			for (j = beg; j <= end; j++)
				glyph_code[(U16(offsets + 2 * i,
					offset + (j - beg) * 2) + delta) & 0xffff] = j;
		} else {
			for (j = beg; j <= end; j++)
				glyph_code[(j + delta) & 0xffff] = j;
		}
	}
}

/* parse otf cmap header */
static void otf_cmap(void *otf, void *cmap)
{
	void *recs = cmap + CMAPLEN;	/* cmap records */
	void *rec;			/* a cmap record */
	void *tab;			/* a cmap subtable */
	int plat, enc;
	int fmt;
	int nrecs = U16(cmap, 2);
	int i;
	for (i = 0; i < nrecs; i++) {
		rec = recs + i * CMAPRECLEN;
		plat = U16(rec, 0);
		enc = U16(rec, 2);
		tab = cmap + U32(rec, 4);
		fmt = U16(tab, 0);
		if (plat == 3 && enc == 1 && fmt == 4)
			otf_cmap4(otf, tab);
	}
}

static void otf_post(void *otf, void *post)
{
	void *post2;			/* version 2.0 header */
	void *index;			/* glyph name indices */
	void *names;			/* glyph names */
	int i, idx;
	int cname = 0;
	if (U32(post, 0) != 0x00020000)
		return;
	post2 = post + 32;
	glyph_n = U16(post2, 0);
	index = post2 + 2;
	names = index + 2 * glyph_n;
	for (i = 0; i < glyph_n; i++) {
		idx = U16(index, 2 * i);
		if (idx <= 257) {
			strcpy(glyph_name[i], macset[idx]);
		} else {
			memcpy(glyph_name[i], names + cname + 1,
				U8(names, cname));
			glyph_name[i][U8(names, cname)] = '\0';
			cname += U8(names, cname) + 1;
		}
	}
}

static void otf_glyf(void *otf, void *glyf)
{
	void *maxp = otf_table(otf, "maxp");
	void *head = otf_table(otf, "head");
	void *loca = otf_table(otf, "loca");
	void *gdat;
	void *gdat_next;
	int n = U16(maxp, 4);
	int fmt = U16(head, 50);
	int i, j;
	for (i = 0; i < n; i++) {
		if (fmt) {
			gdat = glyf + U32(loca, 4 * i);
			gdat_next = glyf + U32(loca, 4 * (i + 1));
		} else {
			gdat = glyf + U16(loca, 2 * i) * 2;
			gdat_next = glyf + U16(loca, 2 * (i + 1)) * 2;
		}
		if (gdat < gdat_next)
			for (j = 0; j < 4; j++)
				glyph_bbox[i][j] = S16(gdat, 2 + 2 * j);
	}
}

static void otf_hmtx(void *otf, void *hmtx)
{
	void *hhea = otf_table(otf, "hhea");
	int n;
	int i;
	n = U16(hhea, 34);
	for (i = 0; i < n; i++)
		glyph_wid[i] = U16(hmtx, i * 4);
	for (i = n; i < glyph_n; i++)
		glyph_wid[i] = glyph_wid[n - 1];
}

static void otf_kern(void *otf, void *kern)
{
	int n;		/* number of kern subtables */
	void *tab;	/* a kern subtable */
	int off = 4;
	int npairs;
	int cov;
	int i, j;
	int c1, c2, val;
	n = U16(kern, 2);
	for (i = 0; i < n; i++) {
		tab = kern + off;
		off += U16(tab, 2);
		cov = U16(tab, 4);
		if ((cov >> 8) == 0 && (cov & 1)) {	/* format 0 */
			npairs = U16(tab, 6);
			for (j = 0; j < npairs; j++) {
				c1 = U16(tab, 14 + 6 * j);
				c2 = U16(tab, 14 + 6 * j + 2);
				val = S16(tab, 14 + 6 * j + 4);
				trfn_kern(glyph_name[c1], glyph_name[c2],
					owid(val));
			}
		}
	}
}

static int coverage(void *cov, int *out)
{
	int fmt = U16(cov, 0);
	int n = U16(cov, 2);
	int beg, end;
	int ncov = 0;
	int i, j;
	if (fmt == 1) {
		for (i = 0; i < n; i++)
			out[ncov++] = U16(cov, 4 + 2 * i);
	}
	if (fmt == 2) {
		for (i = 0; i < n; i++) {
			beg = U16(cov, 4 + 6 * i);
			end = U16(cov, 4 + 6 * i + 2);
			for (j = beg; j <= end; j++)
				out[ncov++] = j;
		}
	}
	return ncov;
}

static int classdef(void *tab, int *gl, int *cls)
{
	int fmt = U16(tab, 0);
	int beg, end;
	int n, ngl = 0;
	int i, j;
	if (fmt == 1) {
		beg = U16(tab, 2);
		ngl = U16(tab, 4);
		for (i = 0; i < ngl; i++) {
			gl[i] = beg + i;
			cls[i] = U16(tab, 6 + 2 * i);
		}
	}
	if (fmt == 2) {
		n = U16(tab, 2);
		for (i = 0; i < n; i++) {
			beg = U16(tab, 4 + 6 * i);
			end = U16(tab, 4 + 6 * i + 2);
			for (j = beg; j <= end; j++) {
				gl[ngl] = j;
				cls[ngl] = U16(tab, 4 + 6 * i + 4);
				ngl++;
			}
		}
	}
	return ngl;
}

static int intcmp(void *v1, void *v2)
{
	return *(int *) v1 - *(int *) v2;
}

int ggrp_make(int *src, int n);

static int ggrp_class(int *src, int *cls, int nsrc, int id)
{
	int g[NGLYPHS];
	int n = 0;
	int i;
	for (i = 0; i < nsrc; i++)
		if (cls[i] == id)
			g[n++] = src[i];
	qsort(g, n, sizeof(g[0]), (void *) intcmp);
	return ggrp_make(g, n);
}

static int ggrp_coverage(int *g, int n)
{
	qsort(g, n, sizeof(g[0]), (void *) intcmp);
	return ggrp_make(g, n);
}

static int valuerecord_len(int fmt)
{
	int off = 0;
	int i;
	for (i = 0; i < 8; i++)
		if (fmt & (1 << i))
			off += 2;
	return off;
}

static void valuerecord_print(int fmt, void *rec)
{
	int vals[8] = {0};
	int off = 0;
	int i;
	for (i = 0; i < 8; i++) {
		if (fmt & (1 << i)) {
			vals[i] = uwid(S16(rec, off));
			off += 2;
		}
	}
	if (fmt)
		printf(":%+d%+d%+d%+d", vals[0], vals[1], vals[2], vals[3]);
}

static int valuerecord_small(int fmt, void *rec)
{
	int off = 0;
	int i;
	for (i = 0; i < 8; i++) {
		if (fmt & (1 << i)) {
			if (abs(uwid(S16(rec, off))) >= MAX(1, kmin))
				return 0;
			off += 2;
		}
	}
	return 1;
}

/* single adjustment positioning */
static void otf_gpostype1(void *otf, void *sub, char *feat)
{
	int fmt = U16(sub, 0);
	int vfmt = U16(sub, 4);
	int cov[NGLYPHS];
	int ncov, nvals;
	int vlen = valuerecord_len(vfmt);
	int i;
	ncov = coverage(sub + U16(sub, 2), cov);
	if (fmt == 1) {
		for (i = 0; i < ncov; i++) {
			printf("gpos %s 1 %s", feat, glyph_name[cov[i]]);
			valuerecord_print(vfmt, sub + 6);
			printf("\n");
		}
	}
	if (fmt == 2) {
		nvals = U16(sub, 6);
		for (i = 0; i < nvals; i++) {
			printf("gpos %s 1 %s", feat, glyph_name[cov[i]]);
			valuerecord_print(vfmt, sub + 8 + i * vlen);
			printf("\n");
		}
	}
}

/* pair adjustment positioning */
static void otf_gpostype2(void *otf, void *sub, char *feat)
{
	int fmt = U16(sub, 0);
	int vfmt1 = U16(sub, 4);	/* valuerecord 1 */
	int vfmt2 = U16(sub, 6);	/* valuerecord 2 */
	int fmtoff1, fmtoff2;
	int vrlen;			/* the length of vfmt1 and vfmt2 */
	int i, j;
	vrlen = valuerecord_len(vfmt1) + valuerecord_len(vfmt2);
	if (fmt == 1) {
		int cov[NGLYPHS];
		int nc1 = U16(sub, 8);
		coverage(sub + U16(sub, 2), cov);
		for (i = 0; i < nc1; i++) {
			void *c2 = sub + U16(sub, 10 + 2 * i);
			int nc2 = U16(c2, 0);
			for (j = 0; j < nc2; j++) {
				int second = U16(c2 + 2 + (2 + vrlen) * j, 0);
				fmtoff1 = 2 + (2 + vrlen) * j + 2;
				fmtoff2 = fmtoff1 + valuerecord_len(vfmt1);
				if (valuerecord_small(vfmt1, c2 + fmtoff1) &&
					valuerecord_small(vfmt2, c2 + fmtoff2))
					continue;
				printf("gpos %s 2", feat);
				printf(" %s", glyph_name[cov[i]]);
				valuerecord_print(vfmt1, c2 + fmtoff1);
				printf(" %s", glyph_name[second]);
				valuerecord_print(vfmt2, c2 + fmtoff2);
				printf("\n");
			}
		}
	}
	if (fmt == 2) {
		int gl1[NGLYPHS], gl2[NGLYPHS];
		int cls1[NGLYPHS], cls2[NGLYPHS];
		int grp1[NGLYPHS], grp2[NGLYPHS];
		int ngl1 = classdef(sub + U16(sub, 8), gl1, cls1);
		int ngl2 = classdef(sub + U16(sub, 10), gl2, cls2);
		int ncls1 = U16(sub, 12);
		int ncls2 = U16(sub, 14);
		for (i = 0; i < ncls1; i++)
			grp1[i] = ggrp_class(gl1, cls1, ngl1, i);
		for (i = 0; i < ncls2; i++)
			grp2[i] = ggrp_class(gl2, cls2, ngl2, i);
		for (i = 0; i < ncls1; i++) {
			for (j = 0; j < ncls2; j++) {
				fmtoff1 = 16 + (i * ncls2 + j) * vrlen;
				fmtoff2 = fmtoff1 + valuerecord_len(vfmt1);
				if (valuerecord_small(vfmt1, sub + fmtoff1) &&
					valuerecord_small(vfmt2, sub + fmtoff2))
					continue;
				printf("gpos %s %d", feat, 2);
				printf(" @%d", grp1[i]);
				valuerecord_print(vfmt1, sub + fmtoff1);
				printf(" @%d", grp2[j]);
				valuerecord_print(vfmt2, sub + fmtoff2);
				printf("\n");
			}
		}
	}
}

/* cursive attachment positioning */
static void otf_gpostype3(void *otf, void *sub, char *feat)
{
	int fmt = U16(sub, 0);
	int cov[NGLYPHS];
	int i, n;
	coverage(sub + U16(sub, 2), cov);
	if (fmt != 1)
		return;
	n = U16(sub, 4);
	for (i = 0; i < n; i++) {
		int prev = U16(sub, 6 + 4 * i);
		int next = U16(sub, 6 + 4 * i + 2);
		printf("gcur %s %s", feat, glyph_name[cov[i]]);
		if (prev)
			printf(" %d %d", uwid(S16(sub, prev + 2)),
					uwid(S16(sub, prev + 4)));
		else
			printf(" - -");
		if (next)
			printf(" %d %d", uwid(S16(sub, next + 2)),
					uwid(S16(sub, next + 4)));
		else
			printf(" - -");
		printf("\n");
	}
}

/* parse the given gpos feature table */
static void otf_gposfeatrec(void *otf, void *gpos, void *featrec)
{
	void *feats = gpos + U16(gpos, 6);
	void *lookups = gpos + U16(gpos, 8);
	void *feat;
	int nlookups;
	char tag[8] = "";
	int i, j;
	memcpy(tag, featrec, 4);
	feat = feats + U16(featrec, 4);
	nlookups = U16(feat, 2);
	for (i = 0; i < nlookups; i++) {
		void *lookup = lookups + U16(lookups, 2 + 2 * U16(feat, 4 + 2 * i));
		int ltype = U16(lookup, 0);
		int ntabs = U16(lookup, 4);
		for (j = 0; j < ntabs; j++) {
			void *tab = lookup + U16(lookup, 6 + 2 * j);
			int type = ltype;
			if (type == 9) {	/* extension positioning */
				type = U16(tab, 2);
				tab = tab + U32(tab, 4);
			}
			switch (type) {
			case 1:
				otf_gpostype1(otf, tab, tag);
				break;
			case 2:
				otf_gpostype2(otf, tab, tag);
				break;
			case 3:
				otf_gpostype3(otf, tab, tag);
				break;
			default:
				otf_unsupported("GPOS", type, 0);
			}
		}
	}
}

/* parse the given gpos language table and its feature tables */
static void otf_gposlang(void *otf, void *gpos, void *lang)
{
	void *feats = gpos + U16(gpos, 6);
	int featidx = U16(lang, 2);
	int nfeat = U16(lang, 4);
	int i;
	if (featidx != 0xffff)
		otf_gposfeatrec(otf, gpos, feats + 2 + 6 * featidx);
	for (i = 0; i < nfeat; i++)
		otf_gposfeatrec(otf, gpos,
				feats + 2 + 6 * U16(lang, 6 + 2 * i));
}

static void otf_gpos(void *otf, void *gpos)
{
	void *scripts = gpos + U16(gpos, 4);
	int nscripts, nlangs;
	void *script;
	void *grec, *lrec;
	char tag[8];
	int i, j;
	nscripts = U16(scripts, 0);
	for (i = 0; i < nscripts; i++) {
		grec = scripts + 2 + 6 * i;
		memcpy(tag, grec, 4);
		tag[4] = '\0';
		if (!trfn_script(tag, nscripts))
			continue;
		script = scripts + U16(grec, 4);
		nlangs = U16(script, 2);
		if (U16(script, 0) && trfn_lang(NULL, nlangs + (U16(script, 0) != 0)))
			otf_gposlang(otf, gpos, script + U16(script, 0));
		for (j = 0; j < nlangs; j++) {
			lrec = script + 4 + 6 * j;
			memcpy(tag, lrec, 4);
			tag[4] = '\0';
			if (trfn_lang(tag, nlangs + (U16(script, 0) != 0)))
				otf_gposlang(otf, gpos, script + U16(lrec, 4));
		}
	}
}

/* gsub context */
struct gctx {
	int bgrp[GCTXLEN];	/* backtrack coverage arrays */
	int igrp[GCTXLEN];	/* input coverage arrays */
	int lgrp[GCTXLEN];	/* lookahead coverage arrays*/
	int bn, in, ln;		/* size of b[], i[], l[] */
	int seqidx;		/* sequence index */
};

static int gctx_len(struct gctx *ctx, int patlen)
{
	return ctx ? ctx->bn + ctx->in + ctx->ln - patlen : 0;
}

static void gctx_backtrack(struct gctx *ctx)
{
	int i;
	if (!ctx)
		return;
	for (i = 0; i < ctx->bn; i++)
		printf(" =@%d", ctx->bgrp[i]);
	for (i = 0; i < ctx->seqidx; i++)
		printf(" =@%d", ctx->igrp[i]);
}

static void gctx_lookahead(struct gctx *ctx, int patlen)
{
	int i;
	if (!ctx)
		return;
	for (i = ctx->seqidx + patlen; i < ctx->in; i++)
		printf(" =@%d", ctx->igrp[i]);
	for (i = 0; i < ctx->ln; i++)
		printf(" =@%d", ctx->lgrp[i]);
}

/* single substitution */
static void otf_gsubtype1(void *otf, void *sub, char *feat, struct gctx *ctx)
{
	int cov[NGLYPHS];
	int fmt = U16(sub, 0);
	int ncov;
	int n;
	int i;
	ncov = coverage(sub + U16(sub, 2), cov);
	if (fmt == 1) {
		for (i = 0; i < ncov; i++) {
			printf("gsub %s %d", feat, 2 + gctx_len(ctx, 1));
			gctx_backtrack(ctx);
			printf(" -%s +%s", glyph_name[cov[i]],
				glyph_name[cov[i] + S16(sub, 4)]);
			gctx_lookahead(ctx, 1);
			printf("\n");
		}
	}
	if (fmt == 2) {
		n = U16(sub, 4);
		for (i = 0; i < n; i++) {
			printf("gsub %s %d", feat, 2 + gctx_len(ctx, 1));
			gctx_backtrack(ctx);
			printf(" -%s +%s", glyph_name[cov[i]],
				glyph_name[U16(sub, 6 + 2 * i)]);
			gctx_lookahead(ctx, 1);
			printf("\n");
		}
	}
}

/* alternate substitution */
static void otf_gsubtype3(void *otf, void *sub, char *feat, struct gctx *ctx)
{
	int cov[NGLYPHS];
	int fmt = U16(sub, 0);
	int n, i, j;
	if (fmt != 1)
		return;
	coverage(sub + U16(sub, 2), cov);
	n = U16(sub, 4);
	for (i = 0; i < n; i++) {
		void *alt = sub + U16(sub, 6 + 2 * i);
		int nalt = U16(alt, 0);
		for (j = 0; j < nalt; j++) {
			printf("gsub %s %d", feat, 2 + gctx_len(ctx, 1));
			gctx_backtrack(ctx);
			printf(" -%s +%s", glyph_name[cov[i]],
				glyph_name[U16(alt, 2 + 2 * j)]);
			gctx_lookahead(ctx, 1);
			printf("\n");
		}
	}
}

/* ligature substitution */
static void otf_gsubtype4(void *otf, void *sub, char *feat, struct gctx *ctx)
{
	int fmt = U16(sub, 0);
	int cov[NGLYPHS];
	int n, i, j, k;
	if (fmt != 1)
		return;
	coverage(sub + U16(sub, 2), cov);
	n = U16(sub, 4);
	for (i = 0; i < n; i++) {
		void *set = sub + U16(sub, 6 + 2 * i);
		int nset = U16(set, 0);
		for (j = 0; j < nset; j++) {
			void *lig = set + U16(set, 2 + 2 * j);
			int nlig = U16(lig, 2);
			printf("gsub %s %d", feat, nlig + 1 + gctx_len(ctx, nlig));
			gctx_backtrack(ctx);
			printf(" -%s", glyph_name[cov[i]]);
			for (k = 0; k < nlig - 1; k++)
				printf(" -%s", glyph_name[U16(lig, 4 + 2 * k)]);
			printf(" +%s", glyph_name[U16(lig, 0)]);
			gctx_lookahead(ctx, nlig);
			printf("\n");
		}
	}
}

/* chaining contextual substitution */
static void otf_gsubtype6(void *otf, void *sub, char *feat, void *gsub)
{
	struct gctx ctx = {{0}};
	void *lookups = gsub + U16(gsub, 8);
	int fmt = U16(sub, 0);
	int cov[NGLYPHS];
	int i, j, nsub, ncov;
	int off = 2;
	if (fmt != 3) {
		otf_unsupported("GSUB", 6, fmt);
		return;
	}
	ctx.bn = U16(sub, off);
	for (i = 0; i < ctx.bn; i++) {
		ncov = coverage(sub + U16(sub, off + 2 + 2 * i), cov);
		ctx.bgrp[i] = ggrp_coverage(cov, ncov);
	}
	off += 2 + 2 * ctx.bn;
	ctx.in = U16(sub, off);
	for (i = 0; i < ctx.in; i++) {
		ncov = coverage(sub + U16(sub, off + 2 + 2 * i), cov);
		ctx.igrp[i] = ggrp_coverage(cov, ncov);
	}
	off += 2 + 2 * ctx.in;
	ctx.ln = U16(sub, off);
	for (i = 0; i < ctx.ln; i ++) {
		ncov = coverage(sub + U16(sub, off + 2 + 2 * i), cov);
		ctx.lgrp[i] = ggrp_coverage(cov, ncov);
	}
	off += 2 + 2 * ctx.ln;
	nsub = U16(sub, off);	/* nsub > 1 is not supported */
	for (i = 0; i < nsub && i < 1; i++) {
		int lidx = U16(sub, off + 2 + 4 * i + 2);
		void *lookup = lookups + U16(lookups, 2 + 2 * lidx);
		int ltype = U16(lookup, 0);
		int ntabs = U16(lookup, 4);
		ctx.seqidx = U16(sub, off + 2 + 4 * i);
		for (j = 0; j < ntabs; j++) {
			void *tab = lookup + U16(lookup, 6 + 2 * j);
			int type = ltype;
			if (type == 7) {	/* extension substitution */
				type = U16(tab, 2);
				tab = tab + U32(tab, 4);
			}
			if (type == 1)
				otf_gsubtype1(otf, tab, feat, &ctx);
			if (type == 3)
				otf_gsubtype3(otf, tab, feat, &ctx);
			if (type == 4)
				otf_gsubtype4(otf, tab, feat, &ctx);
		}
	}
}

/* parse the given gsub feature table */
static void otf_gsubfeatrec(void *otf, void *gsub, void *featrec)
{
	void *feats = gsub + U16(gsub, 6);
	void *lookups = gsub + U16(gsub, 8);
	void *feat;
	int nlookups;
	char tag[8] = "";
	int i, j;
	memcpy(tag, featrec, 4);
	feat = feats + U16(featrec, 4);
	nlookups = U16(feat, 2);
	for (i = 0; i < nlookups; i++) {
		void *lookup = lookups + U16(lookups, 2 + 2 * U16(feat, 4 + 2 * i));
		int ltype = U16(lookup, 0);
		int ntabs = U16(lookup, 4);
		for (j = 0; j < ntabs; j++) {
			void *tab = lookup + U16(lookup, 6 + 2 * j);
			int type = ltype;
			if (type == 7) {	/* extension substitution */
				type = U16(tab, 2);
				tab = tab + U32(tab, 4);
			}
			switch (type) {
			case 1:
				otf_gsubtype1(otf, tab, tag, NULL);
				break;
			case 3:
				otf_gsubtype3(otf, tab, tag, NULL);
				break;
			case 4:
				otf_gsubtype4(otf, tab, tag, NULL);
				break;
			case 6:
				otf_gsubtype6(otf, tab, tag, gsub);
				break;
			default:
				otf_unsupported("GSUB", type, 0);
			}
		}
	}
}

/* parse the given gsub language table and its feature tables */
static void otf_gsublang(void *otf, void *gsub, void *lang)
{
	void *feats = gsub + U16(gsub, 6);
	int featidx = U16(lang, 2);
	int nfeat = U16(lang, 4);
	int i;
	if (featidx != 0xffff)
		otf_gsubfeatrec(otf, gsub, feats + 2 + 6 * featidx);
	for (i = 0; i < nfeat; i++)
		otf_gsubfeatrec(otf, gsub,
				feats + 2 + 6 * U16(lang, 6 + 2 * i));
}

static void otf_gsub(void *otf, void *gsub)
{
	void *scripts = gsub + U16(gsub, 4);
	int nscripts, nlangs;
	void *script;
	void *grec, *lrec;
	char tag[8];
	int i, j;
	nscripts = U16(scripts, 0);
	for (i = 0; i < nscripts; i++) {
		grec = scripts + 2 + 6 * i;
		memcpy(tag, grec, 4);
		tag[4] = '\0';
		if (!trfn_script(tag, nscripts))
			continue;
		script = scripts + U16(scripts + 2 + 6 * i, 4);
		nlangs = U16(script, 2);
		if (U16(script, 0) && trfn_lang(NULL, nlangs + (U16(script, 0) != 0)))
			otf_gsublang(otf, gsub, script + U16(script, 0));
		for (j = 0; j < nlangs; j++) {
			lrec = script + 4 + 6 * j;
			memcpy(tag, lrec, 4);
			tag[4] = '\0';
			if (trfn_lang(tag, nlangs + (U16(script, 0) != 0)))
				otf_gsublang(otf, gsub, script + U16(lrec, 4));
		}
	}
}

int xread(int fd, char *buf, int len)
{
	int nr = 0;
	while (nr < len) {
		int ret = read(fd, buf + nr, len - nr);
		if (ret == -1 && (errno == EAGAIN || errno == EINTR))
			continue;
		if (ret <= 0)
			break;
		nr += ret;
	}
	return nr;
}

static char buf[BUFLEN];

int otf_read(void)
{
	int i;
	if (xread(0, buf, sizeof(buf)) <= 0)
		return 1;
	upm = U16(otf_table(buf, "head"), 18);
	otf_name(buf, otf_table(buf, "name"));
	otf_cmap(buf, otf_table(buf, "cmap"));
	otf_post(buf, otf_table(buf, "post"));
	if (otf_table(buf, "glyf"))
		otf_glyf(buf, otf_table(buf, "glyf"));
	otf_hmtx(buf, otf_table(buf, "hmtx"));
	for (i = 0; i < glyph_n; i++) {
		trfn_char(glyph_name[i], -1,
			glyph_code[i] != 0xffff ? glyph_code[i] : 0,
			owid(glyph_wid[i]),
			owid(glyph_bbox[i][0]), owid(glyph_bbox[i][1]),
			owid(glyph_bbox[i][2]), owid(glyph_bbox[i][3]));
	}
	if (otf_table(buf, "kern"))
		otf_kern(buf, otf_table(buf, "kern"));
	return 0;
}

void otf_feat(int r, int k, int w)
{
	res = r;
	kmin = k;
	warn = w;
	if (otf_table(buf, "GSUB"))
		otf_gsub(buf, otf_table(buf, "GSUB"));
	if (otf_table(buf, "GPOS"))
		otf_gpos(buf, otf_table(buf, "GPOS"));
}

/* glyph groups */
static int *ggrp_g[NGRPS];
static int ggrp_len[NGRPS];
static int ggrp_n;

static int ggrp_find(int *src, int n)
{
	int i, j;
	for (i = 0; i < ggrp_n; i++) {
		if (ggrp_len[i] == n) {
			for (j = 0; j < n; j++)
				if (src[j] != ggrp_g[i][j])
					break;
			if (j == n)
				return i;
		}
	}
	return -1;
}

int ggrp_make(int *src, int n)
{
	int id = ggrp_find(src, n);
	int i;
	if (id >= 0)
		return id;
	id = ggrp_n++;
	ggrp_g[id] = malloc(n * sizeof(ggrp_g[id][0]));
	ggrp_len[id] = n;
	for (i = 0; i < n; i++)
		ggrp_g[id][i] = src[i];
	printf("ggrp %d %d", id, n);
	for (i = 0; i < n; i++)
		printf(" %s", glyph_name[src[i]]);
	printf("\n");
	return id;
}

static char *macset[] = {
	".notdef", ".null", "nonmarkingreturn", "space", "exclam",
	"quotedbl", "numbersign", "dollar", "percent", "ampersand",
	"quotesingle", "parenleft", "parenright", "asterisk", "plus",
	"comma", "hyphen", "period", "slash", "zero",
	"one", "two", "three", "four", "five",
	"six", "seven", "eight", "nine", "colon",
	"semicolon", "less", "equal", "greater", "question",
	"at", "A", "B", "C", "D",
	"E", "F", "G", "H", "I",
	"J", "K", "L", "M", "N",
	"O", "P", "Q", "R", "S",
	"T", "U", "V", "W", "X",
	"Y", "Z", "bracketleft", "backslash", "bracketright",
	"asciicircum", "underscore", "grave", "a", "b",
	"c", "d", "e", "f", "g",
	"h", "i", "j", "k", "l",
	"m", "n", "o", "p", "q",
	"r", "s", "t", "u", "v",
	"w", "x", "y", "z", "braceleft",
	"bar", "braceright", "asciitilde", "Adieresis", "Aring",
	"Ccedilla", "Eacute", "Ntilde", "Odieresis", "Udieresis",
	"aacute", "agrave", "acircumflex", "adieresis", "atilde",
	"aring", "ccedilla", "eacute", "egrave", "ecircumflex",
	"edieresis", "iacute", "igrave", "icircumflex", "idieresis",
	"ntilde", "oacute", "ograve", "ocircumflex", "odieresis",
	"otilde", "uacute", "ugrave", "ucircumflex", "udieresis",
	"dagger", "degree", "cent", "sterling", "section",
	"bullet", "paragraph", "germandbls", "registered", "copyright",
	"trademark", "acute", "dieresis", "notequal", "AE",
	"Oslash", "infinity", "plusminus", "lessequal", "greaterequal",
	"yen", "mu", "partialdiff", "summation", "product",
	"pi", "integral", "ordfeminine", "ordmasculine", "Omega",
	"ae", "oslash", "questiondown", "exclamdown", "logicalnot",
	"radical", "florin", "approxequal", "Delta", "guillemotleft",
	"guillemotright", "ellipsis", "nonbreakingspace", "Agrave", "Atilde",
	"Otilde", "OE", "oe", "endash", "emdash",
	"quotedblleft", "quotedblright", "quoteleft", "quoteright", "divide",
	"lozenge", "ydieresis", "Ydieresis", "fraction", "currency",
	"guilsinglleft", "guilsinglright", "fi", "fl", "daggerdbl",
	"periodcentered", "quotesinglbase", "quotedblbase", "perthousand", "Acircumflex",
	"Ecircumflex", "Aacute", "Edieresis", "Egrave", "Iacute",
	"Icircumflex", "Idieresis", "Igrave", "Oacute", "Ocircumflex",
	"apple", "Ograve", "Uacute", "Ucircumflex", "Ugrave",
	"dotlessi", "circumflex", "tilde", "macron", "breve",
	"dotaccent", "ring", "cedilla", "hungarumlaut", "ogonek",
	"caron", "Lslash", "lslash", "Scaron", "scaron",
	"Zcaron", "zcaron", "brokenbar", "Eth", "eth",
	"Yacute", "yacute", "Thorn", "thorn", "minus",
	"multiply", "onesuperior", "twosuperior", "threesuperior", "onehalf",
	"onequarter", "threequarters", "franc", "Gbreve", "gbreve",
	"Idotaccent", "Scedilla", "scedilla", "Cacute", "cacute",
	"Ccaron", "ccaron", "dcroat",
};
