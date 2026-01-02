/*
 * NEATMKFN - GENERATE NEATROFF FONT DESCRIPTIONS
 *
 * Copyright (C) 2012-2026 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mkfn.h"

#define LEN(a)		((sizeof(a) / sizeof((a)[0])))

static char *mkfn_scripts;	/* filtered scripts */
static char *mkfn_langs;	/* filtered languages */
static char *mkfn_subfont;	/* filtered font */
static char *mkfn_trname;	/* font troff name */
static char *mkfn_psname;	/* font ps name */
static char *mkfn_path;		/* font path */
int mkfn_res = 720;		/* device resolution */
int mkfn_warn;			/* warn about unsupported features */
int mkfn_kmin;			/* minimum kerning value */
int mkfn_swid;			/* space width */
int mkfn_special;		/* special flag */
int mkfn_bbox;			/* include bounding box */
int mkfn_noligs;		/* suppress ligatures */
int mkfn_pos = 1;		/* include glyph positions */
int mkfn_byname;		/* always reference glyphs by name */
int mkfn_dry;			/* generate no output */

/* OpenType specifies a specific feature order for different scripts */
static char *scriptorder[][2] = {
	{"latn", "ccmp,liga,clig,dist,kern,mark,mkmk"},
	{"cyrl", "ccmp,liga,clig,dist,kern,mark,mkmk"},
	{"grek", "ccmp,liga,clig,dist,kern,mark,mkmk"},
	{"armn", "ccmp,liga,clig,dist,kern,mark,mkmk"},
	{"geor", "ccmp,liga,clig,dist,kern,mark,mkmk"},
	{"runr", "ccmp,liga,clig,dist,kern,mark,mkmk"},
	{"ogam", "ccmp,liga,clig,dist,kern,mark,mkmk"},
	{"arab", "ccmp,isol,fina,medi,init,rlig,calt,liga,dlig,cswh,mset,curs,kern,mark,mkmk"},
	{"bugi", "locl,ccmp,rlig,liga,clig,calt,kern,dist,mark,mkmk"},
	{"hang", "ccmp,ljmo,vjmo,tjmo"},
	{"hebr", "ccmp,dlig,kern,mark"},
	{"bng2", "locl,nukt,akhn,rphf,blwf,half,pstf,vatu,cjct,init,pres,abvs,blws,psts,haln,calt,kern,dist,abvm,blwm"},
	{"dev2", "locl,nukt,akhn,rphf,blwf,half,pstf,vatu,cjct,init,pres,abvs,blws,psts,haln,calt,kern,dist,abvm,blwm"},
	{"gjr2", "locl,nukt,akhn,rphf,blwf,half,pstf,vatu,cjct,init,pres,abvs,blws,psts,haln,calt,kern,dist,abvm,blwm"},
	{"gur2", "locl,nukt,akhn,rphf,blwf,half,pstf,vatu,cjct,init,pres,abvs,blws,psts,haln,calt,kern,dist,abvm,blwm"},
	{"knd2", "locl,nukt,akhn,rphf,blwf,half,pstf,vatu,cjct,init,pres,abvs,blws,psts,haln,calt,kern,dist,abvm,blwm"},
	{"mlym", "locl,nukt,akhn,rphf,blwf,half,pstf,vatu,cjct,init,pres,abvs,blws,psts,haln,calt,kern,dist,abvm,blwm"},
	{"ory2", "locl,nukt,akhn,rphf,blwf,half,pstf,vatu,cjct,init,pres,abvs,blws,psts,haln,calt,kern,dist,abvm,blwm"},
	{"tml2", "locl,nukt,akhn,rphf,blwf,half,pstf,vatu,cjct,init,pres,abvs,blws,psts,haln,calt,kern,dist,abvm,blwm"},
	{"tml2", "locl,nukt,akhn,rphf,blwf,half,pstf,vatu,cjct,init,pres,abvs,blws,psts,haln,calt,kern,dist,abvm,blwm"},
	{"telu", "locl,nukt,akhn,rphf,blwf,half,pstf,vatu,cjct,init,pres,abvs,blws,psts,haln,calt,kern,dist,abvm,blwm"},
	{"java", "locl,pref,abvf,blwf,pstf,pres,abvs,blws,psts,ccmp,rlig,liga,clig,calt,kern,dist,mark,mkmk"},
	{"khmr", "pref,blwf,abvf,pstf,pres,blws,abvs,psts,clig,dist,blwm,abvm,mkmk"},
	{"lao ", "ccmp,kern,mark,mkmk"},
	{"mym2", "locl,rphf,pref,blwf,pstf,pres,abvs,blws,psts,kern,dist,mark,mkmk"},
	{"sinh", "locl,ccmp,akhn,rphf,vatu,pstf,pres,abvs,blws,psts,kern,dist,abvm,blwm"},
	{"syrc", "stch,ccmp,isol,fina,fin2,fin3,medi,med2,init,rlig,calt,liga,dlig,kern,mark,mkmk"},
	{"thaa", "kern,mark"},
	{"thai", "ccmp,kern,mark,mkmk"},
	{"tibt", "ccmp,abvs,blws,calt,liga,kern,abvm,blwm,mkmk"},
};

/* return 1 if the given script is to be included */
int mkfn_script(char *script, int nscripts)
{
	/* fill mkfn_scripts (if unspecified) in the first call */
	if (!mkfn_scripts) {
		if (nscripts == 1 || !script)
			return 1;
		if (!strcmp("DFLT", script))
			mkfn_scripts = "DFLT";
		else
			mkfn_scripts = "latn";
	}
	if (!strcmp("list", mkfn_scripts))
		printf("%s\n", script ? script : "");
	if (strchr(script, ' '))
		*strchr(script, ' ') = '\0';
	return !!strstr(mkfn_scripts, script);
}

/* return 1 if the given language is to be included */
int mkfn_lang(char *lang, int nlangs)
{
	if (!mkfn_langs)
		return 1;
	if (!lang)
		lang = "";
	if (!strcmp("list", mkfn_langs))
		printf("%s\n", lang);
	if (strchr(lang, ' '))
		*strchr(lang, ' ') = '\0';
	return !!strstr(mkfn_langs, lang);
}

/* return 1 if the given font is to be included */
int mkfn_font(char *font)
{
	static int idx;			/* font index */
	idx++;
	if (!mkfn_subfont)
		return idx == 1;
	if (!strcmp("list", mkfn_subfont))
		printf("%s\n", font);
	if (mkfn_subfont[0] && isdigit((unsigned char) mkfn_subfont[0]))
		if (atoi(mkfn_subfont) == idx)
			return 1;
	return !strcmp(mkfn_subfont, font);
}

/* return the rank of the given feature, for the current script */
int mkfn_featrank(char *scrp, char *feat)
{
	static char **order;
	int i;
	if (!order || strcmp(scrp, order[0])) {
		order = NULL;
		for (i = 0; i < LEN(scriptorder); i++)
			if (!strcmp(scrp, scriptorder[i][0]))
				order = scriptorder[i];
	}
	if (order && strstr(order[1], feat))
		return strstr(order[1], feat) - order[1];
	return 1000;
}

void mkfn_header(char *fontname)
{
	if (mkfn_dry)
		return;
	if (mkfn_trname)
		printf("name %s\n", mkfn_trname);
	if (mkfn_psname)
		printf("fontname %s\n", mkfn_psname);
	if (!mkfn_psname && fontname && fontname[0])
		printf("fontname %s\n", fontname);
	if (mkfn_path)
		printf("fontpath %s\n", mkfn_path);
	trfn_header();
	if (mkfn_special)
		printf("special\n");
	trfn_cdefs();
}

int otf_read(void);
int afm_read(void);

static char *usage =
	"Usage: mktrfn [options] <input >output\n"
	"Options:\n"
	"  -a      \tread an AFM file (default)\n"
	"  -o      \tread a TTF or an OTF file\n"
	"  -s      \tspecial font\n"
	"  -p name \toverride font postscript name\n"
	"  -t name \tset font troff name\n"
	"  -f path \tset font path\n"
	"  -r res  \tset device resolution (720)\n"
	"  -k kmin \tspecify the minimum amount of kerning (0)\n"
	"  -b      \tinclude glyph bounding boxes\n"
	"  -l      \tsuppress the ligatures line\n"
	"  -n      \tsuppress glyph positions\n"
	"  -g      \talways reference glyphs by name in OTF rules\n"
	"  -S scrs \tcomma-separated list of scripts to include (list to list)\n"
	"  -L langs\tcomma-separated list of languages to include (list to list)\n"
	"  -F font \tfont name or index in a font collection (list to list)\n"
	"  -w      \twarn about unsupported font features\n";

int main(int argc, char *argv[])
{
	int afm = 1;
	int i;
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		switch (argv[i][1]) {
		case 'a':
			afm = 1;
			break;
		case 'b':
			mkfn_bbox = 1;
			break;
		case 'f':
			mkfn_path = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 'F':
			mkfn_subfont = argv[i][2] ? argv[i] + 2 : argv[++i];
			mkfn_dry = !strcmp("list", mkfn_subfont);
			break;
		case 'g':
			mkfn_byname = 1;
			break;
		case 'k':
			mkfn_kmin = atoi(argv[i][2] ? argv[i] + 2 : argv[++i]);
			break;
		case 'l':
			mkfn_noligs = 1;
			break;
		case 'L':
			mkfn_langs = argv[i][2] ? argv[i] + 2 : argv[++i];
			mkfn_dry = !strcmp("list", mkfn_langs);
			break;
		case 'n':
			mkfn_pos = 0;
			break;
		case 'o':
			afm = 0;
			break;
		case 'p':
			mkfn_psname = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 'r':
			mkfn_res = atoi(argv[i][2] ? argv[i] + 2 : argv[++i]);
			break;
		case 's':
			mkfn_special = 1;
			break;
		case 'S':
			mkfn_scripts = argv[i][2] ? argv[i] + 2 : argv[++i];
			mkfn_dry = !strcmp("list", mkfn_scripts);
			break;
		case 't':
			mkfn_trname = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 'w':
			mkfn_warn = 1;
			break;
		default:
			fprintf(stderr, "%s", usage);
			return 1;
		}
	}
	trfn_init();
	if ((afm ? afm_read() : otf_read())) {
		fprintf(stderr, "neatmkfn: cannot parse the font\n");
		trfn_done();
		return 1;
	}
	trfn_done();
	return 0;
}
