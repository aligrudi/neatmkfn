/*
 * mktrfn - produce troff font descriptions
 *
 * Copyright (C) 2012-2014 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * This program is released under the Modified BSD license.
 */
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "trfn.h"

#define TOKLEN		256

static char *trfn_scripts;	/* filtered scripts */
static char *trfn_langs;	/* filtered languages */
static char *trfn_featranks;	/* manual feature ordering */

static char *afm_charfield(char *s, char *d)
{
	while (*s && !isspace(*s) && *s != ';')
		*d++ = *s++;
	while (isspace(*s) || *s == ';')
		s++;
	*d = '\0';
	return s;
}

static int afm_read(void)
{
	char ln[1024];
	char ch[TOKLEN] = "", pos[TOKLEN] = "";
	char c1[TOKLEN] = "", c2[TOKLEN] = "";
	char wid[TOKLEN] = "", field[TOKLEN] = "";
	char llx[TOKLEN] = "0", lly[TOKLEN] = "0";
	char urx[TOKLEN] = "0", ury[TOKLEN] = "0";
	char *s;
	while (fgets(ln, sizeof(ln), stdin)) {
		if (ln[0] == '#')
			continue;
		if (!strncmp("FontName ", ln, 8)) {
			sscanf(ln, "FontName %s", ch);
			trfn_psfont(ch);
			continue;
		}
		if (!strncmp("StartCharMetrics", ln, 16))
			break;
	}
	while (fgets(ln, sizeof(ln), stdin)) {
		if (ln[0] == '#')
			continue;
		if (!strncmp("EndCharMetrics", ln, 14))
			break;
		s = ln;
		while (*s) {
			s = afm_charfield(s, field);
			if (!strcmp("C", field)) {
				s = afm_charfield(s, pos);
				continue;
			}
			if (!strcmp("WX", field)) {
				s = afm_charfield(s, wid);
				continue;
			}
			if (!strcmp("N", field)) {
				s = afm_charfield(s, ch);
				continue;
			}
			if (!strcmp("B", field)) {
				s = afm_charfield(s, llx);
				s = afm_charfield(s, lly);
				s = afm_charfield(s, urx);
				s = afm_charfield(s, ury);
				continue;
			}
			if (!strcmp("L", field)) {
				s = afm_charfield(s, c1);
				s = afm_charfield(s, c2);
				continue;
			}
			break;
		}
		if (ch[0] && pos[0] && wid[0])
			trfn_char(ch, atoi(pos), 0, atoi(wid),
				atoi(llx), atoi(lly), atoi(urx), atoi(ury));
	}
	while (fgets(ln, sizeof(ln), stdin)) {
		if (ln[0] == '#')
			continue;
		if (!strncmp("StartKernPairs", ln, 14))
			break;
	}
	while (fgets(ln, sizeof(ln), stdin)) {
		if (ln[0] == '#')
			continue;
		if (!strncmp("EndKernPairs", ln, 12))
			break;
		if (sscanf(ln, "KPX %s %s %s", c1, c2, wid) == 3)
			trfn_kern(c1, c2, atoi(wid));
	}
	return 0;
}

/* return 1 if script script is to be included */
int trfn_script(char *script, int nscripts)
{
	if (!trfn_scripts)
		return nscripts == 1 || !script ||
			!strcmp("DFLT", script) || !strcmp("latn", script);
	if (!strcmp("help", trfn_scripts))
		fprintf(stderr, "script: %s\n", script ? script : "");
	if (strchr(script, ' '))
		*strchr(script, ' ') = '\0';
	return !!strstr(trfn_scripts, script);
}

/* return 1 if language lang is to be included */
int trfn_lang(char *lang, int nlangs)
{
	if (!trfn_langs)
		return nlangs == 1 || !lang;
	if (!lang)
		lang = "";
	if (!strcmp("help", trfn_langs))
		fprintf(stderr, "lang: %s\n", lang);
	if (strchr(lang, ' '))
		*strchr(lang, ' ') = '\0';
	return !!strstr(trfn_langs, lang);
}

int trfn_featrank(char *feat)
{
	char *s = trfn_featranks ? strstr(trfn_featranks, feat) : NULL;
	return s ? s - trfn_featranks : 1000;
}

int otf_read(void);
void otf_feat(int res, int kmin, int warn);

static char *usage =
	"Usage: mktrfn [options] <input >output\n"
	"Options:\n"
	"  -a      \tread an AFM file (default)\n"
	"  -o      \tread an OTF file\n"
	"  -s      \tspecial font\n"
	"  -p name \toverride font postscript name\n"
	"  -t name \tset font troff name\n"
	"  -r res  \tset device resolution (720)\n"
	"  -k kmin \tspecify the minimum amount of kerning (0)\n"
	"  -b      \tinclude glyph bounding boxes\n"
	"  -S scrs \tcomma-separated list of scripts to include (help to list)\n"
	"  -L langs\tcomma-separated list of languages to include (help to list)\n"
	"  -O order\tfeatures to apply first (required only for some languages)\n"
	"  -w      \twarn about unsupported font features\n";

int main(int argc, char *argv[])
{
	int afm = 1;
	int res = 720;
	int spc = 0;
	int kmin = 0;
	int bbox = 0;
	int warn = 0;
	int i;
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		switch (argv[i][1]) {
		case 'a':
			afm = 1;
			break;
		case 'b':
			bbox = 1;
			break;
		case 'k':
			kmin = atoi(argv[i][2] ? argv[i] + 2 : argv[++i]);
			break;
		case 'L':
			trfn_langs = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 'o':
			afm = 0;
			break;
		case 'O':
			trfn_featranks = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 'p':
			trfn_psfont(argv[i][2] ? argv[i] + 2 : argv[++i]);
			break;
		case 'r':
			res = atoi(argv[i][2] ? argv[i] + 2 : argv[++i]);
			break;
		case 's':
			spc = 1;
			break;
		case 'S':
			trfn_scripts = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 't':
			trfn_trfont(argv[i][2] ? argv[i] + 2 : argv[++i]);
			break;
		case 'w':
			warn = 1;
			break;
		default:
			printf("%s", usage);
			return 0;
		}
	}
	trfn_init(res, spc, kmin, bbox);
	if (afm)
		afm_read();
	else
		otf_read();
	trfn_print();
	if (!afm)
		otf_feat(res, kmin, warn);
	trfn_done();
	return 0;
}
