#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "trfn.h"

#define NGLYPHS			(1 << 14)
#define GNLEN			(64)
#define BUFLEN			(1 << 23)
#define OWID(w)			((w) * 1000 / (upm))

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

static char *macset[];

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

/* parse otf cmap format 4 subtable */
static void otf_cmap4(void *otf, void *cmap4)
{
	int nsegs;
	void *ends, *begs, *deltas, *offsets;
	void *idarray;
	int beg, end, delta, offset;
	int i, j;
	nsegs = U16(cmap4, 6) / 2;
	ends = cmap4 + 14;
	begs = ends + 2 * nsegs + 2;
	deltas = begs + 2 * nsegs;
	offsets = deltas + 2 * nsegs;
	idarray = offsets + 2 * nsegs;
	for (i = 0; i < nsegs; i++) {
		beg = U16(begs, 2 * i);
		end = U16(ends, 2 * i);
		delta = U16(deltas, 2 * i);
		offset = U16(offsets, 2 * i);
		if (offset) {
			for (j = beg; j <= end; j++)
				glyph_code[U16(offsets + i * 2,
						offset + (j - beg) * 2)] = j;
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
					OWID(val));
			}
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
	otf_cmap(buf, otf_table(buf, "cmap"));
	otf_post(buf, otf_table(buf, "post"));
	if (otf_table(buf, "glyf"))
		otf_glyf(buf, otf_table(buf, "glyf"));
	otf_hmtx(buf, otf_table(buf, "hmtx"));
	for (i = 0; i < glyph_n; i++) {
		trfn_char(glyph_name[i], -1,
			glyph_code[i] != 0xffff ? glyph_code[i] : 0,
			OWID(glyph_wid[i]),
			OWID(glyph_bbox[i][0]), OWID(glyph_bbox[i][1]),
			OWID(glyph_bbox[i][2]), OWID(glyph_bbox[i][3]));
	}
	otf_kern(buf, otf_table(buf, "kern"));
	return 0;
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
