#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sbuf.h"
#include "trfn.h"

#define MAX(a, b)	((a) < (b) ? (b) : (a))
#define LEN(a)		(sizeof(a) / sizeof((a)[0]))

#define NGLYPHS		(1 << 16)
#define NLOOKUPS	(1 << 12)
#define GNLEN		(64)
#define NGRPS		2048

#define U32(buf, off)		(htonl(*(u32 *) ((buf) + (off))))
#define U16(buf, off)		(htons(*(u16 *) ((buf) + (off))))
#define U8(buf, off)		(*(u8 *) ((buf) + (off)))
#define S16(buf, off)		((s16) htons(*(u16 *) ((buf) + (off))))
#define S32(buf, off)		((s32) htonl(*(u32 *) ((buf) + (off))))

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
static int warn;		/* report unsupported tables */

static char *macset[];
static char *stdset[];

static int owid(int w)
{
	return (w < 0 ? w * 1000 - upm / 2 : w * 1000 + upm / 2) / upm;
}

static int uwid(int w)
{
	int d = 72000 / res;
	return (w < 0 ? owid(w) - d / 20 : owid(w) + d / 20) * 10 / d;
}

/* weather the script is right-to-left */
static int otf_r2l(char *feat)
{
	char *scrp = strchr(feat, ':') + 1;
	return !strcmp("arab", scrp) || !strcmp("hebr", scrp);
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
	int nrecs = U16(otf, 4);
	int i;
	for (i = 0; i < nrecs; i++) {
		void *rec = otf + 12 + i * 16;	/* an otf table record */
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
	int nrecs = U16(cmap, 2);
	int i;
	for (i = 0; i < nrecs; i++) {
		void *rec = cmap + 4 + i * 8;	/* a cmap record */
		int plat = U16(rec, 0);
		int enc = U16(rec, 2);
		void *tab = cmap + U32(rec, 4);	/* a cmap subtable */
		int fmt = U16(tab, 0);
		if (plat == 3 && enc == 1 && fmt == 4)
			otf_cmap4(otf, tab);
	}
}

static void otf_post(void *otf, void *post)
{
	void *post2;			/* version 2.0 header */
	void *index;			/* glyph name indices */
	void *names;			/* glyph names */
	int cname = 0;
	int i;
	if (U32(post, 0) != 0x20000)
		return;
	post2 = post + 32;
	glyph_n = U16(post2, 0);
	index = post2 + 2;
	names = index + 2 * glyph_n;
	for (i = 0; i < glyph_n; i++) {
		int idx = U16(index, 2 * i);
		if (idx < 258) {
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
	if (!glyph_n)
		glyph_n = n;
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
	int off = 4;
	int i, j;
	int n = U16(kern, 2);		/* number of kern subtables */
	for (i = 0; i < n; i++) {
		void *tab = kern + off;	/* a kern subtable */
		int cov = U16(tab, 4);
		off += U16(tab, 2);
		if ((cov >> 8) == 0 && (cov & 1)) {	/* format 0 */
			int npairs = U16(tab, 6);
			for (j = 0; j < npairs; j++) {
				int c1 = U16(tab, 14 + 6 * j);
				int c2 = U16(tab, 14 + 6 * j + 2);
				int val = S16(tab, 14 + 6 * j + 4);
				trfn_kern(glyph_name[c1], glyph_name[c2],
					owid(val));
			}
		}
	}
}

static int *coverage(void *cov, int *ncov)
{
	int fmt = U16(cov, 0);
	int n = U16(cov, 2);
	int beg, end;
	int i, j;
	int *out = malloc(glyph_n * sizeof(*out));
	int cnt = 0;
	if (fmt == 1) {
		for (i = 0; i < n; i++)
			out[cnt++] = U16(cov, 4 + 2 * i);
	}
	if (fmt == 2) {
		for (i = 0; i < n; i++) {
			beg = U16(cov, 4 + 6 * i);
			end = U16(cov, 4 + 6 * i + 2);
			for (j = beg; j <= end; j++)
				out[cnt++] = j;
		}
	}
	if (ncov)
		*ncov = cnt;
	return out;
}

static int classdef(void *tab, int *gl, int *cls)
{
	int fmt = U16(tab, 0);
	int ngl = 0;
	int i, j;
	if (fmt == 1) {
		int beg = U16(tab, 2);
		ngl = U16(tab, 4);
		for (i = 0; i < ngl; i++) {
			gl[i] = beg + i;
			cls[i] = U16(tab, 6 + 2 * i);
		}
	}
	if (fmt == 2) {
		int n = U16(tab, 2);
		for (i = 0; i < n; i++) {
			int beg = U16(tab, 4 + 6 * i);
			int end = U16(tab, 4 + 6 * i + 2);
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

static int ggrp_make(int *src, int n);

static int ggrp_class(int *src, int *cls, int nsrc, int id)
{
	int *g = malloc(nsrc * sizeof(g[0]));
	int n = 0;
	int i;
	int grp;
	for (i = 0; i < nsrc; i++)
		if (cls[i] == id)
			g[n++] = src[i];
	qsort(g, n, sizeof(g[0]), (void *) intcmp);
	grp = ggrp_make(g, n);
	free(g);
	return grp;
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
	int *cov;
	int ncov, nvals;
	int vlen = valuerecord_len(vfmt);
	int i;
	cov = coverage(sub + U16(sub, 2), &ncov);
	if (fmt == 1) {
		for (i = 0; i < ncov; i++) {
			if (valuerecord_small(vfmt, sub + 6))
				continue;
			printf("gpos %s 1 %s", feat, glyph_name[cov[i]]);
			valuerecord_print(vfmt, sub + 6);
			printf("\n");
		}
	}
	if (fmt == 2) {
		nvals = U16(sub, 6);
		for (i = 0; i < nvals; i++) {
			if (valuerecord_small(vfmt, sub + 6))
				continue;
			printf("gpos %s 1 %s", feat, glyph_name[cov[i]]);
			valuerecord_print(vfmt, sub + 8 + i * vlen);
			printf("\n");
		}
	}
	free(cov);
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
		int nc1 = U16(sub, 8);
		int *cov = coverage(sub + U16(sub, 2), NULL);
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
		free(cov);
	}
	if (fmt == 2) {
		static int gl1[NGLYPHS], gl2[NGLYPHS];
		static int cls1[NGLYPHS], cls2[NGLYPHS];
		static int grp1[NGLYPHS], grp2[NGLYPHS];
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
	int *cov, *icov, *ocov;
	int i, n;
	int icnt = 0;
	int ocnt = 0;
	int igrp, ogrp;
	if (fmt != 1)
		return;
	cov = coverage(sub + U16(sub, 2), NULL);
	n = U16(sub, 4);
	icov = malloc(n * sizeof(icov[0]));
	ocov = malloc(n * sizeof(ocov[0]));
	for (i = 0; i < n; i++)
		if (U16(sub, 6 + 4 * i))
			ocov[ocnt++] = cov[i];
	for (i = 0; i < n; i++)
		if (U16(sub, 6 + 4 * i + 2))
			icov[icnt++] = cov[i];
	igrp = ggrp_coverage(icov, icnt);
	ogrp = ggrp_coverage(ocov, ocnt);
	free(icov);
	free(ocov);
	for (i = 0; i < n; i++) {
		int prev = U16(sub, 6 + 4 * i);
		int next = U16(sub, 6 + 4 * i + 2);
		if (prev) {
			int dx = -uwid(S16(sub, prev + 2));
			int dy = -uwid(S16(sub, prev + 4));
			if (otf_r2l(feat)) {
				dx += uwid(glyph_wid[cov[i]]);
			}
			printf("gpos %s 2 @%d %s:%+d%+d%+d%+d\n",
				feat, igrp, glyph_name[cov[i]],
				0, 0, dx, dy);
		}
		if (next) {
			int dx = uwid(S16(sub, next + 2)) - uwid(glyph_wid[cov[i]]);
			int dy = uwid(S16(sub, next + 4));
			if (otf_r2l(feat)) {
				dx += uwid(glyph_wid[cov[i]]);
			}
			printf("gpos %s 2 %s @%d:%+d%+d%+d%+d\n",
				feat, glyph_name[cov[i]], ogrp,
				0, 0, dx, dy);
		}
	}
	free(cov);
}

/* mark-to-base attachment positioning */
static void otf_gpostype4(void *otf, void *sub, char *feat)
{
	int fmt = U16(sub, 0);
	int *mcov;		/* mark coverage */
	int *bcov;		/* base coverage */
	int cgrp[1024];		/* glyph groups assigned to classes */
	int bgrp;		/* the group assigned to base glyphs */
	int mcnt;		/* mark coverage size */
	int bcnt;		/* base coverage size */
	int ccnt;		/* class count */
	void *marks;		/* mark array table */
	void *bases;		/* base array table */
	int i, j;
	if (fmt != 1)
		return;
	mcov = coverage(sub + U16(sub, 2), &mcnt);
	bcov = coverage(sub + U16(sub, 4), &bcnt);
	ccnt = U16(sub, 6);
	marks = sub + U16(sub, 8);
	bases = sub + U16(sub, 10);
	bgrp = ggrp_coverage(bcov, bcnt);
	for (i = 0; i < ccnt; i++) {
		int *grp = malloc(mcnt * sizeof(grp[0]));
		int cnt = 0;
		for (j = 0; j < mcnt; j++)
			if (U16(marks, 2 + 4 * j) == i)
				grp[cnt++] = mcov[j];
		cgrp[i] = ggrp_coverage(grp, cnt);
		free(grp);
	}
	for (i = 0; i < mcnt; i++) {
		void *mark = marks + U16(marks, 2 + 4 * i + 2);	/* mark anchor */
		int dx = -uwid(S16(mark, 2));
		int dy = -uwid(S16(mark, 4));
		if (otf_r2l(feat)) {
			dx += uwid(glyph_wid[mcov[i]]);
			dy = -dy;
		}
		printf("gpos %s 2 @%d %s:%+d%+d%+d%+d\n",
			feat, bgrp, glyph_name[mcov[i]], dx, dy, 0, 0);
	}
	for (i = 0; i < bcnt; i++) {
		for (j = 0; j < ccnt; j++) {
			void *base = bases + U16(bases, 2 + ccnt * 2 * i + 2 * j);
			int dx = uwid(S16(base, 2)) - uwid(glyph_wid[bcov[i]]);
			int dy = uwid(S16(base, 4));
			if (otf_r2l(feat)) {
				dx += uwid(glyph_wid[bcov[i]]);
				dy = -dy;
			}
			printf("gpos %s 2 %s @%d:%+d%+d%+d%+d\n",
				feat, glyph_name[bcov[i]], cgrp[j], dx, dy, 0, 0);
		}
	}
	free(mcov);
	free(bcov);
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
	int *cov;
	int fmt = U16(sub, 0);
	int ncov;
	int i;
	cov = coverage(sub + U16(sub, 2), &ncov);
	if (fmt == 1) {
		for (i = 0; i < ncov; i++) {
			if (cov[i] + S16(sub, 4) >= glyph_n)
				continue;
			printf("gsub %s %d", feat, 2 + gctx_len(ctx, 1));
			gctx_backtrack(ctx);
			printf(" -%s +%s", glyph_name[cov[i]],
				glyph_name[cov[i] + S16(sub, 4)]);
			gctx_lookahead(ctx, 1);
			printf("\n");
		}
	}
	if (fmt == 2) {
		int n = U16(sub, 4);
		for (i = 0; i < n; i++) {
			printf("gsub %s %d", feat, 2 + gctx_len(ctx, 1));
			gctx_backtrack(ctx);
			printf(" -%s +%s", glyph_name[cov[i]],
				glyph_name[U16(sub, 6 + 2 * i)]);
			gctx_lookahead(ctx, 1);
			printf("\n");
		}
	}
	free(cov);
}

/* alternate substitution */
static void otf_gsubtype3(void *otf, void *sub, char *feat, struct gctx *ctx)
{
	int *cov;
	int fmt = U16(sub, 0);
	int n, i, j;
	if (fmt != 1)
		return;
	cov = coverage(sub + U16(sub, 2), NULL);
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
	free(cov);
}

/* ligature substitution */
static void otf_gsubtype4(void *otf, void *sub, char *feat, struct gctx *ctx)
{
	int fmt = U16(sub, 0);
	int *cov;
	int n, i, j, k;
	if (fmt != 1)
		return;
	cov = coverage(sub + U16(sub, 2), NULL);
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
	free(cov);
}

/* chaining contextual substitution */
static void otf_gsubtype6(void *otf, void *sub, char *feat, void *gsub)
{
	struct gctx ctx = {{0}};
	void *lookups = gsub + U16(gsub, 8);
	int fmt = U16(sub, 0);
	int *cov;
	int i, j, nsub, ncov;
	int off = 2;
	if (fmt != 3) {
		otf_unsupported("GSUB", 6, fmt);
		return;
	}
	ctx.bn = U16(sub, off);
	for (i = 0; i < ctx.bn; i++) {
		cov = coverage(sub + U16(sub, off + 2 + 2 * i), &ncov);
		ctx.bgrp[i] = ggrp_coverage(cov, ncov);
		free(cov);
	}
	off += 2 + 2 * ctx.bn;
	ctx.in = U16(sub, off);
	for (i = 0; i < ctx.in; i++) {
		cov = coverage(sub + U16(sub, off + 2 + 2 * i), &ncov);
		ctx.igrp[i] = ggrp_coverage(cov, ncov);
		free(cov);
	}
	off += 2 + 2 * ctx.in;
	ctx.ln = U16(sub, off);
	for (i = 0; i < ctx.ln; i ++) {
		cov = coverage(sub + U16(sub, off + 2 + 2 * i), &ncov);
		ctx.lgrp[i] = ggrp_coverage(cov, ncov);
		free(cov);
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

/* an otf gsub/gpos lookup */
struct otflookup {
	char scrp[8];		/* script name */
	char lang[8];		/* language name */
	char feat[8];		/* feature name */
	int lookup;		/* index into the lookup table */
};

/* parse the given gsub/gpos feature table */
static int otf_featrec(void *otf, void *gtab, void *featrec,
			char *stag, char *ltag,
			struct otflookup *lookups, int lookups_n)
{
	void *feats = gtab + U16(gtab, 6);
	void *feat = feats + U16(featrec, 4);
	int n = U16(feat, 2);
	int i, j;
	for (i = 0; i < n; i++) {
		int lookup = U16(feat, 4 + 2 * i);	/* lookup index */
		/* do not store features common to all languages in a script */
		for (j = 0; j < lookups_n; j++)
			if (lookups[j].lookup == lookup && !lookups[j].lang[0])
				if (!strcmp(lookups[j].scrp, stag))
					break;
		if (j == lookups_n) {
			memcpy(lookups[j].feat, featrec, 4);
			lookups[j].feat[4] = '\0';
			strcpy(lookups[j].scrp, stag);
			strcpy(lookups[j].lang, ltag);
			lookups[j].lookup = U16(feat, 4 + 2 * i);
			lookups_n++;
		}
	}
	return lookups_n;
}

/* parse the given language table and its feature tables */
static int otf_lang(void *otf, void *gtab, void *lang, char *stag, char *ltag,
		struct otflookup *lookups, int lookups_n)
{
	void *feats = gtab + U16(gtab, 6);
	int featidx = U16(lang, 2);
	int nfeat = U16(lang, 4);
	int i;
	if (featidx != 0xffff)
		lookups_n = otf_featrec(otf, gtab, feats + 2 + 6 * featidx,
				stag, ltag, lookups, lookups_n);
	for (i = 0; i < nfeat; i++)
		lookups_n = otf_featrec(otf, gtab, feats + 2 + 6 * U16(lang, 6 + 2 * i),
				stag, ltag, lookups, lookups_n);
	return lookups_n;
}

/* return lookup table tag (i.e. liga:latn:ENG); returns a static buffer */
static char *lookuptag(struct otflookup *lu)
{
	static char tag[16];
	sprintf(tag, "%s:%s", lu->feat, lu->scrp[0] ? lu->scrp : "DFLT");
	if (lu->lang[0])
		sprintf(strchr(tag, '\0'), ":%s", lu->lang);
	return tag;
}

static int lookupcmp(void *v1, void *v2)
{
	struct otflookup *l1 = v1;
	struct otflookup *l2 = v2;
	if (strcmp(l1->scrp, l2->scrp))
		return strcmp(l1->scrp, l2->scrp);
	if (trfn_featrank(l1->scrp, l1->feat) != trfn_featrank(l1->scrp, l2->feat))
		return trfn_featrank(l1->scrp, l1->feat) - trfn_featrank(l1->scrp, l2->feat);
	return l1->lookup - l2->lookup;
}

/* extract lookup tables for all features of the given gsub/gpos table */
static int otf_gtab(void *otf, void *gpos, struct otflookup *lookups)
{
	void *scripts = gpos + U16(gpos, 4);
	int nscripts, nlangs;
	void *script;
	char stag[8], ltag[8];		/* script and language tags */
	int i, j;
	int n = 0;
	nscripts = U16(scripts, 0);
	for (i = 0; i < nscripts; i++) {
		void *grec = scripts + 2 + 6 * i;
		memcpy(stag, grec, 4);
		stag[4] = '\0';
		if (!trfn_script(stag, nscripts))
			continue;
		script = scripts + U16(grec, 4);
		nlangs = U16(script, 2);
		if (U16(script, 0) && trfn_lang(NULL, nlangs + (U16(script, 0) != 0)))
			n = otf_lang(otf, gpos, script + U16(script, 0),
						stag, "", lookups, n);
		for (j = 0; j < nlangs; j++) {
			void *lrec = script + 4 + 6 * j;
			memcpy(ltag, lrec, 4);
			ltag[4] = '\0';
			if (trfn_lang(ltag, nlangs + (U16(script, 0) != 0)))
				n = otf_lang(otf, gpos, script + U16(lrec, 4),
						stag, ltag, lookups, n);
		}
	}
	qsort(lookups, n, sizeof(lookups[0]), (void *) lookupcmp);
	return n;
}

static void otf_gpos(void *otf, void *gpos)
{
	struct otflookup lookups[NLOOKUPS];
	void *lookuplist = gpos + U16(gpos, 8);
	int nlookups = otf_gtab(otf, gpos, lookups);
	int i, j;
	for (i = 0; i < nlookups; i++) {
		void *lookup = lookuplist + U16(lookuplist, 2 + 2 * lookups[i].lookup);
		int ltype = U16(lookup, 0);
		int ntabs = U16(lookup, 4);
		char *tag = lookuptag(&lookups[i]);
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
			case 4:
				otf_gpostype4(otf, tab, tag);
				break;
			default:
				otf_unsupported("GPOS", type, 0);
			}
		}
	}
}

static void otf_gsub(void *otf, void *gsub)
{
	struct otflookup lookups[NLOOKUPS];
	void *lookuplist = gsub + U16(gsub, 8);
	int nlookups = otf_gtab(otf, gsub, lookups);
	int i, j;
	for (i = 0; i < nlookups; i++) {
		void *lookup = lookuplist + U16(lookuplist, 2 + 2 * lookups[i].lookup);
		int ltype = U16(lookup, 0);
		int ntabs = U16(lookup, 4);
		char *tag = lookuptag(&lookups[i]);
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

/* read a cff offset, which has sz bytes */
static int cff_int(void *tab, int off, int sz)
{
	int i;
	int n = 0;
	for (i = 0; i < sz; i++)
		n = n * 256 + U8(tab, off + i);
	return n;
}

/* cff dict operand/operator */
static int cff_op(void *tab, int off, int *val)
{
	int b0 = U8(tab, off);
	int i;
	if (b0 >= 32 && b0 <= 246) {
		*val = b0 - 139;
		return 1;
	}
	if (b0 >= 247 && b0 <= 250) {
		*val = (b0 - 247) * 256 + U8(tab, off + 1) + 108;
		return 2;
	}
	if (b0 >= 251 && b0 <= 254) {
		*val = -(b0 - 251) * 256 - U8(tab, off + 1) - 108;
		return 2;
	}
	if (b0 == 28) {
		*val = (U8(tab, off + 1) << 8) | U8(tab, off + 2);
		return 3;
	}
	if (b0 == 29) {
		*val = (U8(tab, off + 1) << 24) | (U8(tab, off + 2) << 16) |
			(U8(tab, off + 3) << 8) | U8(tab, off + 4);
		return 5;
	}
	if (b0 == 30) {
		for (i = 1; i < 32; i++) {
			int nib = U8(tab, off + i);
			if ((nib & 0x0f) == 0x0f || (nib & 0xf0) == 0xf0)
				break;
		}
		*val = 0;
		return i + 1;
	}
	*val = b0;
	return 1;
}

static int cffidx_cnt(void *idx)
{
	return U16(idx, 0);
}

static void *cffidx_get(void *idx, int i)
{
	int cnt = U16(idx, 0);
	int sz = U8(idx, 2);
	return idx + 3 + (cnt + 1) * sz - 1 + cff_int(idx, 3 + i * sz, sz);
}

static int cffidx_len(void *idx, int i)
{
	return cffidx_get(idx, i + 1) - cffidx_get(idx, i);
}

static void *cffidx_end(void *idx)
{
	return cffidx_get(idx, cffidx_cnt(idx));
}

/* obtain the value of the given key from a cff dict */
static int cffdict_get(void *dict, int len, int key, int *args)
{
	int off = 0;
	int op = 0;
	int val = 0;
	/* operators: keys (one or two bytes); operands: values */
	while (off < len) {
		val = op;
		if (args) {
			memmove(args + 1, args + 0, 3 * sizeof(args[0]));
			args[0] = val;
		}
		off += cff_op(dict, off, &op);
		if (op == 12) {			/* two-byte operator */
			off += cff_op(dict, off, &op);
			op += 1200;
		}
		if (op == key)
			return val;
	}
	return 0;
}

static void cff_char(void *stridx, int id, char *dst)
{
	int len;
	if (id < 391) {
		strcpy(dst, stdset[id]);
		return;
	}
	id -= 391;
	len = cffidx_len(stridx, id);
	if (len >= GNLEN)
		len = GNLEN - 1;
	if (id < cffidx_cnt(stridx)) {
		memcpy(dst, cffidx_get(stridx, id), len);
		dst[len] = '\0';
	}
}

static void otf_cff(void *otf, void *cff)
{
	void *nameidx;		/* name index */
	void *topidx;		/* top dict index */
	void *stridx;		/* string idx */
	void *chridx;		/* charstrings index */
	void *charset;		/* charset offset */
	int bbox[4] = {0};
	int i, j;
	if (U8(cff, 0) != 1)
		return;
	nameidx = cff + U8(cff, 2);
	topidx = cffidx_end(nameidx);
	if (cffidx_cnt(nameidx) < 1)
		return;
	stridx = cffidx_end(topidx);
	chridx = cff + cffdict_get(cffidx_get(topidx, 0),
			cffidx_len(topidx, 0), 17, NULL);
	charset = cff + cffdict_get(cffidx_get(topidx, 0),
			cffidx_len(topidx, 0), 15, NULL);
	glyph_n = cffidx_cnt(chridx);
	strcpy(glyph_name[0], ".notdef");
	/* read charset: glyph to character name */
	if (U8(charset, 0) == 0) {
		for (i = 0; i < glyph_n; i++)
			cff_char(stridx, U16(charset, 1 + i * 2),
				glyph_name[i + 1]);
	}
	if (U8(charset, 0) == 1 || U8(charset, 0) == 2) {
		int g = 1;
		int sz = U8(charset, 0) == 1 ? 3 : 4;
		for (i = 0; g < glyph_n; i++) {
			int sid = U16(charset, 1 + i * sz);
			int cnt = cff_int(charset, 1 + i * sz + 2, sz - 2);
			for (j = 0; j <= cnt && g < glyph_n; j++) {
				cff_char(stridx, sid + j, glyph_name[g]);
				g++;
			}
		}
	}
	/* use font bbox for all glyphs */
	cffdict_get(cffidx_get(topidx, 0), cffidx_len(topidx, 0), 5, bbox);
	for (i = 1; i < glyph_n; i++)
		for (j = 0; j < 4; j++)
			glyph_bbox[i][j] = bbox[3 - j];
}

static void *otf_input(int fd)
{
	struct sbuf *sb = sbuf_make();
	char buf[1 << 12];
	int nr = 0;
	while ((nr = read(fd, buf, sizeof(buf))) > 0)
		sbuf_mem(sb, buf, nr);
	return sbuf_done(sb);
}

static char *otf_buf;

int otf_read(void)
{
	int i;
	otf_buf = otf_input(0);
	upm = U16(otf_table(otf_buf, "head"), 18);
	otf_name(otf_buf, otf_table(otf_buf, "name"));
	otf_cmap(otf_buf, otf_table(otf_buf, "cmap"));
	otf_post(otf_buf, otf_table(otf_buf, "post"));
	if (otf_table(otf_buf, "glyf"))
		otf_glyf(otf_buf, otf_table(otf_buf, "glyf"));
	if (otf_table(otf_buf, "CFF "))
		otf_cff(otf_buf, otf_table(otf_buf, "CFF "));
	for (i = 0; i < glyph_n; i++) {
		if (!glyph_name[i][0]) {
			if (glyph_code[i])
				sprintf(glyph_name[i], "uni%04X", glyph_code[i]);
			else
				sprintf(glyph_name[i], "gl%05X", i);
		}
	}
	otf_hmtx(otf_buf, otf_table(otf_buf, "hmtx"));
	for (i = 0; i < glyph_n; i++) {
		trfn_char(glyph_name[i], -1,
			glyph_code[i] != 0xffff ? glyph_code[i] : 0,
			owid(glyph_wid[i]),
			owid(glyph_bbox[i][0]), owid(glyph_bbox[i][1]),
			owid(glyph_bbox[i][2]), owid(glyph_bbox[i][3]));
	}
	if (otf_table(otf_buf, "kern"))
		otf_kern(otf_buf, otf_table(otf_buf, "kern"));
	return 0;
}

void otf_feat(int r, int k, int w)
{
	res = r;
	kmin = k;
	warn = w;
	if (otf_table(otf_buf, "GSUB"))
		otf_gsub(otf_buf, otf_table(otf_buf, "GSUB"));
	if (otf_table(otf_buf, "GPOS"))
		otf_gpos(otf_buf, otf_table(otf_buf, "GPOS"));
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

static int ggrp_make(int *src, int n)
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

static char *stdset[] = {
	".notdef", "space", "exclam", "quotedbl", "numbersign",
	"dollar", "percent", "ampersand", "quoteright", "parenleft",
	"parenright", "asterisk", "plus", "comma", "hyphen",
	"period", "slash", "zero", "one", "two",
	"three", "four", "five", "six", "seven",
	"eight", "nine", "colon", "semicolon", "less",
	"equal", "greater", "question", "at", "A",
	"B", "C", "D", "E", "F",
	"G", "H", "I", "J", "K",
	"L", "M", "N", "O", "P",
	"Q", "R", "S", "T", "U",
	"V", "W", "X", "Y", "Z",
	"bracketleft", "backslash", "bracketright", "asciicircum", "underscore",
	"quoteleft", "a", "b", "c", "d",
	"e", "f", "g", "h", "i",
	"j", "k", "l", "m", "n",
	"o", "p", "q", "r", "s",
	"t", "u", "v", "w", "x",
	"y", "z", "braceleft", "bar", "braceright",
	"asciitilde", "exclamdown", "cent", "sterling", "fraction",
	"yen", "florin", "section", "currency", "quotesingle",
	"quotedblleft", "guillemotleft", "guilsinglleft", "guilsinglright", "fi",
	"fl", "endash", "dagger", "daggerdbl", "periodcentered",
	"paragraph", "bullet", "quotesinglbase", "quotedblbase", "quotedblright",
	"guillemotright", "ellipsis", "perthousand", "questiondown", "grave",
	"acute", "circumflex", "tilde", "macron", "breve",
	"dotaccent", "dieresis", "ring", "cedilla", "hungarumlaut",
	"ogonek", "caron", "emdash", "AE", "ordfeminine",
	"Lslash", "Oslash", "OE", "ordmasculine", "ae",
	"dotlessi", "lslash", "oslash", "oe", "germandbls",
	"onesuperior", "logicalnot", "mu", "trademark", "Eth",
	"onehalf", "plusminus", "Thorn", "onequarter", "divide",
	"brokenbar", "degree", "thorn", "threequarters", "twosuperior",
	"registered", "minus", "eth", "multiply", "threesuperior",
	"copyright", "Aacute", "Acircumflex", "Adieresis", "Agrave",
	"Aring", "Atilde", "Ccedilla", "Eacute", "Ecircumflex",
	"Edieresis", "Egrave", "Iacute", "Icircumflex", "Idieresis",
	"Igrave", "Ntilde", "Oacute", "Ocircumflex", "Odieresis",
	"Ograve", "Otilde", "Scaron", "Uacute", "Ucircumflex",
	"Udieresis", "Ugrave", "Yacute", "Ydieresis", "Zcaron",
	"aacute", "acircumflex", "adieresis", "agrave", "aring",
	"atilde", "ccedilla", "eacute", "ecircumflex", "edieresis",
	"egrave", "iacute", "icircumflex", "idieresis", "igrave",
	"ntilde", "oacute", "ocircumflex", "odieresis", "ograve",
	"otilde", "scaron", "uacute", "ucircumflex", "udieresis",
	"ugrave", "yacute", "ydieresis", "zcaron", "exclamsmall",
	"Hungarumlautsmall", "dollaroldstyle", "dollarsuperior", "ampersandsmall", "Acutesmall",
	"parenleftsuperior", "parenrightsuperior", "twodotenleader", "onedotenleader", "zerooldstyle",
	"oneoldstyle", "twooldstyle", "threeoldstyle", "fouroldstyle", "fiveoldstyle",
	"sixoldstyle", "sevenoldstyle", "eightoldstyle", "nineoldstyle", "commasuperior",
	"threequartersemdash", "periodsuperior", "questionsmall", "asuperior", "bsuperior",
	"centsuperior", "dsuperior", "esuperior", "isuperior", "lsuperior",
	"msuperior", "nsuperior", "osuperior", "rsuperior", "ssuperior",
	"tsuperior", "ff", "ffi", "ffl", "parenleftinferior",
	"parenrightinferior", "Circumflexsmall", "hyphensuperior", "Gravesmall", "Asmall",
	"Bsmall", "Csmall", "Dsmall", "Esmall", "Fsmall",
	"Gsmall", "Hsmall", "Ismall", "Jsmall", "Ksmall",
	"Lsmall", "Msmall", "Nsmall", "Osmall", "Psmall",
	"Qsmall", "Rsmall", "Ssmall", "Tsmall", "Usmall",
	"Vsmall", "Wsmall", "Xsmall", "Ysmall", "Zsmall",
	"colonmonetary", "onefitted", "rupiah", "Tildesmall", "exclamdownsmall",
	"centoldstyle", "Lslashsmall", "Scaronsmall", "Zcaronsmall", "Dieresissmall",
	"Brevesmall", "Caronsmall", "Dotaccentsmall", "Macronsmall", "figuredash",
	"hypheninferior", "Ogoneksmall", "Ringsmall", "Cedillasmall", "questiondownsmall",
	"oneeighth", "threeeighths", "fiveeighths", "seveneighths", "onethird",
	"twothirds", "zerosuperior", "foursuperior", "fivesuperior", "sixsuperior",
	"sevensuperior", "eightsuperior", "ninesuperior", "zeroinferior", "oneinferior",
	"twoinferior", "threeinferior", "fourinferior", "fiveinferior", "sixinferior",
	"seveninferior", "eightinferior", "nineinferior", "centinferior", "dollarinferior",
	"periodinferior", "commainferior", "Agravesmall", "Aacutesmall", "Acircumflexsmall",
	"Atildesmall", "Adieresissmall", "Aringsmall", "AEsmall", "Ccedillasmall",
	"Egravesmall", "Eacutesmall", "Ecircumflexsmall", "Edieresissmall", "Igravesmall",
	"Iacutesmall", "Icircumflexsmall", "Idieresissmall", "Ethsmall", "Ntildesmall",
	"Ogravesmall", "Oacutesmall", "Ocircumflexsmall", "Otildesmall", "Odieresissmall",
	"OEsmall", "Oslashsmall", "Ugravesmall", "Uacutesmall", "Ucircumflexsmall",
	"Udieresissmall", "Yacutesmall", "Thornsmall", "Ydieresissmall", "001.000",
	"001.001", "001.002", "001.003", "Black", "Bold",
	"Book", "Light", "Medium", "Regular", "Roman",
	"Semibold",
};
