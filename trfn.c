#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sbuf.h"
#include "tab.h"
#include "trfn.h"
#include "trfn_ch.h"

#define WX(w)		(((w) < 0 ? (w) - trfn_div / 2 : (w) + trfn_div / 2) / trfn_div)
#define LEN(a)		((sizeof(a) / sizeof((a)[0])))
#define HEXDIGS		"0123456789abcdef"
#define NCHAR		8	/* number of characters per glyph */
#define GNLEN		64	/* glyph name length */
#define AGLLEN		8192	/* adobe glyphlist length */
#define NSUBS		2048	/* number of substitutions */
#define NPSAL		32	/* number of substitutions per glyph */

static struct sbuf sbuf_char;	/* characters */
static struct sbuf sbuf_kern;	/* kerning pairs */
static int trfn_div;		/* divisor of widths */
static int trfn_swid;		/* space width */
static int trfn_special;	/* special flag */
static int trfn_kmin;		/* minimum kerning value */
static char trfn_ligs[8192];	/* font ligatures */
static char trfn_trname[256];	/* font troff name */
static char trfn_psname[256];	/* font ps name */
/* glyph substition */
static char subs_src[NSUBS][GNLEN];
static char subs_dst[NSUBS][GNLEN];
static int subs_n;

/* adobe glyphlist mapping */
static char agl_key[AGLLEN][GNLEN];
static char agl_val[AGLLEN][GNLEN];
static int agl_n;

/* lookup tables */
static struct tab *tab_agl;
static struct tab *tab_alts;
static struct tab *tab_ctyp;

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
	int l;
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
		l = 0;
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
		if (s[i] && strchr(digs, tolower(s[i])))
			n = n * 16 + (strchr(digs, tolower(s[i])) - digs);
		else
			break;
	}
	return len == 1 ? n << 4 : n;
}

static int agl_read(char *path)
{
	FILE *fin = fopen(path, "r");
	char ln[GNLEN];
	char val[GNLEN];
	char *s, *d;
	int i;
	if (!fin)
		return 1;
	while (fgets(ln, sizeof(ln), fin)) {
		s = strchr(ln, ';');
		if (ln[0] == '#' || !s)
			continue;
		*s++ = '\0';
		d = val;
		while (s && *s) {
			while (*s == ' ')
				s++;
			utf8put(&d, hexval(s, 6));
			s = strchr(s, ' ');
		}
		*d = '\0';
		strcpy(agl_key[agl_n], ln);
		strcpy(agl_val[agl_n], val);
		agl_n++;
	}
	fclose(fin);
	tab_agl = tab_alloc(agl_n);
	for (i = 0; i < agl_n; i++)
		tab_put(tab_agl, agl_key[i], agl_val[i]);
	return 0;
}

static char *agl_map(char *s)
{
	return tab_get(tab_agl, s);
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

void trfn_sub(char *c1, char *c2)
{
	if (subs_n < NSUBS && !strchr(c1, '.')) {
		strcpy(subs_src[subs_n], c1);
		strcpy(subs_dst[subs_n], c2);
		subs_n++;
	}
}

/* return the list of postscript glyph aliases of character c */
static void trfn_subs(char *c, char **a)
{
	char *dot;
	int i, subs = 0;
	/* adding c itself to the list of aliases only if not substituded */
	for (i = 0; i < subs_n; i++)
		if (!strcmp(c, subs_src[i]))
			subs = 1;
	dot = strrchr(c, '.');
	if (!subs && (!dot || !strcmp(".isol", dot) || !strcmp(".init", dot) ||
				!strcmp(".fina", dot) || !strcmp(".medi", dot)))
			*a++ = c;
	/* adding aliases added via trfn_subs() */
	for (i = 0; i < subs_n; i++)
		if (!strcmp(c, subs_dst[i]))
			*a++ = subs_src[i];
	*a++ = NULL;
}

static int trfn_name(char *dst, char *src)
{
	char ch[GNLEN];
	char *d = dst;
	char *s;
	int i;
	if (!src || src[0] == '.')
		return 1;
	while (*src && *src != '.') {
		s = ch;
		if (src[0] == '_')
			src++;
		while (*src && *src != '_' && *src != '.')
			*s++ = *src++;
		*s = '\0';
		if (agl_map(ch)) {
			strcpy(d, agl_map(ch));
			for (i = 0; i < LEN(agl_exceptions); i++)
				if (!strcmp(agl_exceptions[i][0], d))
					strcpy(d, agl_exceptions[i][1]);
			d = strchr(d, '\0');
		} else if (ch[0] == 'u' && ch[1] == 'n' && ch[2] == 'i') {
			for (i = 0; strlen(ch + 3 + 4 * i) >= 4; i++)
				utf8put(&d, hexval(ch + 3 + 4 * i, 4));
		} else if (ch[0] == 'u' && ch[1] && strchr(HEXDIGS, tolower(ch[1]))) {
			utf8put(&d, hexval(ch + 1, 6));
		} else if (achar_map(ch)) {
			utf8put(&d, achar_map(ch));
		} else {
			return 1;
		}
	}
	ashape(dst, src);
	return 0;
}

static void trfn_lig(char *c)
{
	int i;
	if (c[0] && c[1] && strlen(c) > utf8len((unsigned char) c[0])) {
		sprintf(strchr(trfn_ligs, '\0'), "%s ", c);
		return;
	}
	for (i = 0; i < LEN(ligs); i++)
		if (!strcmp(ligs[i], c))
			sprintf(strchr(trfn_ligs, '\0'), "%s ", c);
}

static int trfn_type(char *c)
{
	struct ctype *t = tab_get(tab_ctyp, c);
	return t ? t->type : 3;
}

void trfn_char(char *psname, char *n, int wid, int typ)
{
	char uc[GNLEN];			/* mapping unicode character */
	char *a_ps[NPSAL] = {NULL};	/* postscript glyph substitutions */
	char **a_tr;			/* troff character names */
	char pos[GNLEN] = "";		/* postscript character position/name */
	int i_ps = 0;			/* current name in a_ps */
	/* initializing character attributes */
	if (trfn_name(uc, psname))
		strcpy(uc, "---");
	if (n && atoi(n) >= 0 && atoi(n) < 256)
		strcpy(pos, n);
	if (!n && !strchr(psname, '.') && !uc[1] && uc[0] >= 32 && uc[0] <= 125)
		sprintf(pos, "%d", uc[0]);
	if (typ < 0)
		typ = trfn_type(!strchr(psname, '.') ? uc : "");
	/* printing troff charset */
	trfn_subs(psname, a_ps);
	for (i_ps = 0; !i_ps || a_ps[i_ps]; i_ps++) {
		if (trfn_name(uc, a_ps[i_ps]))
			strcpy(uc, "---");
		if (strchr(uc, ' ')) {		/* space not allowed in char names */
			if (!trfn_swid && !strcmp(" ", uc))
				trfn_swid = WX(wid);
			continue;
		}
		if (strcmp("---", uc))
			trfn_lig(uc);
		sbuf_printf(&sbuf_char, "char %s\t%d\t%d\t%s\t%s\n",
				uc, WX(wid), typ, psname, pos);
		a_tr = tab_get(tab_alts, uc);
		while (a_tr && *a_tr)
			sbuf_printf(&sbuf_char, "char %s\t\"\n", *a_tr++);
	}
}

void trfn_kern(char *c1, char *c2, int x)
{
	if (abs(WX(x)) >= WX(trfn_kmin))
		sbuf_printf(&sbuf_kern, "kern %s\t%s\t%d\n", c1, c2, WX(x));
}

void trfn_trfont(char *name)
{
	if (!trfn_trname[0])
		strcpy(trfn_trname, name);
}

void trfn_psfont(char *name)
{
	if (!trfn_psname[0])
		strcpy(trfn_psname, name);
}

void trfn_print(void)
{
	if (trfn_trname[0])
		printf("name %s\n", trfn_trname);
	if (trfn_psname[0])
		printf("fontname %s\n", trfn_psname);
	printf("spacewidth %d\n", trfn_swid);
	printf("ligatures %s0\n", trfn_ligs);
	if (trfn_special)
		printf("special\n");
	printf("%s", sbuf_buf(&sbuf_char));
	printf("%s", sbuf_buf(&sbuf_kern));
}

void trfn_init(int res, int spc, int kmin)
{
	int i;
	trfn_div = 7200 / res;
	trfn_special = spc;
	trfn_kmin = kmin;
	if (agl_read("glyphlist.txt"))
		fprintf(stderr, "mktrfn: could not open glyphlist.txt\n");
	sbuf_init(&sbuf_char);
	sbuf_init(&sbuf_kern);
	tab_alts = tab_alloc(LEN(alts));
	tab_ctyp = tab_alloc(LEN(ctype));
	for (i = 0; i < LEN(alts); i++)
		tab_put(tab_alts, alts[i][0], alts[i] + 1);
	for (i = 0; i < LEN(ctype); i++)
		tab_put(tab_ctyp, ctype[i].ch, &ctype[i]);
}

void trfn_done(void)
{
	sbuf_done(&sbuf_char);
	sbuf_done(&sbuf_kern);
	tab_free(tab_alts);
	tab_free(tab_ctyp);
	if (tab_agl)
		tab_free(tab_agl);
}
