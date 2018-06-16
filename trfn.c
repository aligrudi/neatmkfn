#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mkfn.h"
#include "trfn_agl.h"
#include "trfn_ch.h"

#define LEN(a)		((sizeof(a) / sizeof((a)[0])))
#define HEXDIGS		"0123456789ABCDEF"
#define NCHAR		8	/* number of characters per glyph */
#define GNLEN		64	/* glyph name length */

/* for buffering the output */
static struct sbuf *sbuf_char;	/* character definitions */
/* handling ligatures */
static char mkfn_ligs[8192];	/* font ligatures */
static char mkfn_ligs2[8192];	/* font ligatures, whose length is two */
/* character type */
static int mkfn_asc;		/* minimum height of glyphs with ascender */
static int mkfn_desc;		/* minimum depth of glyphs with descender */
/* lookup tables */
static struct tab *tab_agl;	/* adobe glyph list table */
static struct tab *tab_alts;	/* character aliases table */

static int utf8len(int c)
{
	if (c > 0 && c <= 0x7f)
		return 1;
	if (c >= 0xfc)
		return 6;
	if (c >= 0xf8)
		return 5;
	if (c >= 0xf0)
		return 4;
	if (c >= 0xe0)
		return 3;
	if (c >= 0xc0)
		return 2;
	return c != 0;
}

static int utf8get(char **src)
{
	int result;
	int l = 1;
	char *s = *src;
	if (~((unsigned char) **src) & 0xc0)
		return (unsigned char) *(*src)++;
	while (l < 6 && (unsigned char) *s & (0x40 >> l))
		l++;
	result = (0x3f >> l) & (unsigned char) *s++;
	while (l--)
		result = (result << 6) | ((unsigned char) *s++ & 0x3f);
	*src = s;
	return result;
}

static void utf8put(char **d, int c)
{
	int l = 0;
	if (c > 0xffff) {
		*(*d)++ = 0xf0 | (c >> 18);
		l = 3;
	} else if (c > 0x7ff) {
		*(*d)++ = 0xe0 | (c >> 12);
		l = 2;
	} else if (c > 0x7f) {
		*(*d)++ = 0xc0 | (c >> 6);
		l = 1;
	} else {
		*(*d)++ = c > 0 ? c : ' ';
	}
	while (l--)
		*(*d)++ = 0x80 | ((c >> (l * 6)) & 0x3f);
	**d = '\0';
}

static int hexval(char *s, int len)
{
	char *digs = HEXDIGS;
	int n = 0;
	int i;
	for (i = 0; i < len; i++) {
		if (s[i] && strchr(digs, s[i]))
			n = n * 16 + (strchr(digs, s[i]) - digs);
		else
			break;
	}
	return i < 4 ? -1 : n;
}

static int agl_map(char *d, char *s)
{
	char *u = tab_get(tab_agl, s);	/* unicode code point like "FB8E" */
	if (!u)
		return 1;
	while (u && *u) {
		while (*u == ' ')
			u++;
		utf8put(&d, hexval(u, 6));
		u = strchr(u, ' ');
	}
	*d = '\0';
	return 0;
}

static int achar_map(char *name)
{
	int i;
	for (i = 0; i < LEN(achars); i++) {
		struct achar *a = &achars[i];
		if (!strncmp(a->name, name, strlen(a->name))) {
			char *postfix = name + strlen(a->name);
			if (!*postfix)
				return a->c;
			if (!strcmp("isolated", postfix))
				return a->s ? a->s : a->c;
			if (!strcmp("initial", postfix))
				return a->i ? a->i : a->c;
			if (!strcmp("medial", postfix))
				return a->m ? a->m : a->c;
			if (!strcmp("final", postfix))
				return a->f ? a->f : a->c;
		}
	}
	return 0;
}

static int achar_shape(int c, int pjoin, int njoin)
{
	int i;
	for (i = 0; i < LEN(achars); i++) {
		struct achar *a = &achars[i];
		if (a->c == c) {
			if (!pjoin && !njoin)
				return a->c;
			if (!pjoin && njoin)
				return a->i ? a->i : a->c;
			if (pjoin && njoin)
				return a->m ? a->m : a->c;
			if (pjoin && !njoin)
				return a->f ? a->f : a->c;
		}
	}
	return c;
}

static void ashape(char *str, char *ext)
{
	int s[NCHAR];
	char *src = str;
	int i, l;
	int bjoin = !strcmp(".medi", ext) || !strcmp(".fina", ext);
	int ejoin = !strcmp(".medi", ext) || !strcmp(".init", ext);
	for (l = 0; l < NCHAR && *src; l++)
		s[l] = utf8get(&src);
	for (i = 0; i < l; i++)
		s[i] = achar_shape(s[i], i > 0 || bjoin, i < l - 1 || ejoin);
	for (i = 0; i < l; i++)
		utf8put(&str, s[i]);
}

/* find the utf-8 name of src with the given unicode codepoint */
static int trfn_name(char *dst, char *src, int codepoint)
{
	char ch[GNLEN];
	char *d = dst;
	char *s;
	int i;
	if (codepoint) {
		utf8put(&dst, codepoint);
		return 0;
	}
	if (!src || src[0] == '.')
		return 1;
	while (*src && *src != '.') {
		s = ch;
		if (src[0] == '_')
			src++;
		while (*src && *src != '_' && *src != '.')
			*s++ = *src++;
		*s = '\0';
		if (!agl_map(d, ch)) {
			d = strchr(d, '\0');
		} else if (ch[0] == 'u' && ch[1] == 'n' &&
				ch[2] == 'i' && hexval(ch + 3, 4) > 0) {
			for (i = 0; strlen(ch + 3 + 4 * i) >= 4; i++)
				utf8put(&d, hexval(ch + 3 + 4 * i, 4));
		} else if (ch[0] == 'u' && hexval(ch + 1, 4) > 0) {
			utf8put(&d, hexval(ch + 1, 6));
		} else if (achar_map(ch)) {
			utf8put(&d, achar_map(ch));
		} else {
			return 1;
		}
	}
	ashape(dst, src);
	return *src && strcmp(src, ".medi") && strcmp(src, ".fina") &&
			strcmp(src, ".init") && strcmp(src, ".isol");
}

static void trfn_aglexceptions(char *dst)
{
	int i;
	for (i = 0; i < LEN(agl_exceptions); i++)
		if (!strcmp(agl_exceptions[i][0], dst))
			strcpy(dst, agl_exceptions[i][1]);
}

static void trfn_ligput(char *c)
{
	char *dst = strlen(c) == 2 ? mkfn_ligs2 : mkfn_ligs;
	sprintf(strchr(dst, '\0'), "%s ", c);
}

static void trfn_lig(char *c)
{
	int i;
	for (i = 0; i < LEN(agl_exceptions); i++)
		if (!strcmp(agl_exceptions[i][1], c))
			return;
	if (c[0] && c[1] && strlen(c) > utf8len((unsigned char) c[0])) {
		trfn_ligput(c);
	} else {
		for (i = 0; i < LEN(ligs_utf8); i++)
			if (!strcmp(ligs_utf8[i][0], c))
				trfn_ligput(ligs_utf8[i][1]);
	}
}

static int trfn_type(char *s, int lly, int ury)
{
	int typ = 0;
	int c = !s[0] || s[1] ? 0 : (unsigned char) *s;
	if (c == 't' && !mkfn_asc)
		mkfn_asc = ury;
	if ((c == 'g' || c == 'j' || c == 'p' || c == 'q' || c == 'y') &&
			(!mkfn_desc || mkfn_desc < lly))
		mkfn_desc = lly;
	if (!mkfn_desc || !mkfn_asc) {
		if (c > 0 && c < 128)
			return ctype_ascii[c];
		return 3;
	}
	if (!mkfn_desc || lly <= mkfn_desc)
		typ |= 1;
	if (!mkfn_asc || ury >= mkfn_asc)
		typ |= 2;
	return typ;
}

/* n is the position and u is the unicode codepoint */
void mkfn_char(char *psname, int n, int u, int wid,
		int llx, int lly, int urx, int ury)
{
	char uc[GNLEN];			/* mapping unicode character */
	char **a_tr;			/* troff character names */
	char pos[GNLEN] = "";		/* postscript character position/name */
	int typ;			/* character type */
	/* initializing character attributes */
	if (trfn_name(uc, psname, u))
		strcpy(uc, "---");
	trfn_aglexceptions(uc);
	if (mkfn_pos && n >= 0 && n < 256)
		sprintf(pos, "%d", n);
	if (mkfn_pos && n < 0 && !uc[1] && uc[0] >= 32 && uc[0] <= 125)
		if (!strchr(psname, '.'))
			sprintf(pos, "%d", uc[0]);
	typ = trfn_type(!strchr(psname, '.') ? uc : "", lly, ury);
	if (!mkfn_swid && (!strcmp(" ", uc) || !strcmp(" ", uc)))
		mkfn_swid = wid;
	/* printing troff charset */
	if (isspace((unsigned char) uc[0]) || strchr(uc, ' '))
		strcpy(uc, "---");	/* space not allowed in char names */
	if (strcmp("---", uc))
		trfn_lig(uc);
	sbuf_printf(sbuf_char, "char %s\t%d", uc, wid);
	if (mkfn_bbox && (llx || lly || urx || ury))
		sbuf_printf(sbuf_char, ",%d,%d,%d,%d", llx, lly, urx, ury);
	sbuf_printf(sbuf_char, "\t%d\t%s\t%s\n", typ, psname, pos);
	a_tr = tab_get(tab_alts, uc);
	while (a_tr && *a_tr)
		sbuf_printf(sbuf_char, "char %s\t\"\n", *a_tr++);
}

void mkfn_kern(char *c1, char *c2, int x)
{
	if (x && abs(x) >= mkfn_kmin)
		if (!mkfn_dry)
			printf("kern %s\t%s\t%d\n", c1, c2, x);
}

/* print spacewidth and ligature lines */
void trfn_header(void)
{
	printf("spacewidth %d\n", mkfn_swid);
	if (!mkfn_noligs)
		printf("ligatures %s%s0\n", mkfn_ligs, mkfn_ligs2);
}

/* print character definitions */
void trfn_cdefs(void)
{
	fputs(sbuf_buf(sbuf_char), stdout);
}

void trfn_init(void)
{
	int i;
	sbuf_char = sbuf_make();
	tab_agl = tab_alloc(LEN(agl));
	for (i = 0; i < LEN(agl); i++)
		tab_put(tab_agl, agl[i][0], agl[i][1]);
	tab_alts = tab_alloc(LEN(alts));
	for (i = 0; i < LEN(alts); i++)
		tab_put(tab_alts, alts[i][0], alts[i] + 1);
}

void trfn_done(void)
{
	sbuf_free(sbuf_char);
	tab_free(tab_alts);
	if (tab_agl)
		tab_free(tab_agl);
}
