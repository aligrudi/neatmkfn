/*
 * NEATMKFN - GENERATE NEATROFF FONT DESCRIPTIONS
 *
 * Copyright (C) 2012-2016 Ali Gholami Rudi <ali at rudi dot ir>
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "trfn.h"

#define LEN(a)		((sizeof(a) / sizeof((a)[0])))

static char *trfn_scripts;	/* filtered scripts */
static char *trfn_langs;	/* filtered languages */
static char *trfn_order;	/* feature ordering */

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
int trfn_script(char *script, int nscripts)
{
	int i;
	trfn_order = NULL;
	for (i = 0; i < LEN(scriptorder); i++)
		if (script && !strcmp(script, scriptorder[i][0]))
			trfn_order = scriptorder[i][1];
	/* fill trfn_scripts (if unspecified) in the first call */
	if (!trfn_scripts) {
		if (nscripts == 1 || !script)
			return 1;
		if (!strcmp("DFLT", script))
			trfn_scripts = "DFLT";
		else
			trfn_scripts = "latn";
	}
	if (!strcmp("help", trfn_scripts))
		fprintf(stderr, "script: %s\n", script ? script : "");
	if (strchr(script, ' '))
		*strchr(script, ' ') = '\0';
	return !!strstr(trfn_scripts, script);
}

/* return 1 if the given language is to be included */
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

/* return the rank of the given feature, for the current script */
int trfn_featrank(char *feat)
{
	char *s = trfn_order ? strstr(trfn_order, feat) : NULL;
	return s ? s - trfn_order : 1000;
}

int otf_read(void);
int afm_read(void);
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
	"  -l      \tsuppress the ligatures line\n"
	"  -n      \tsuppress glyph positions\n"
	"  -S scrs \tcomma-separated list of scripts to include (help to list)\n"
	"  -L langs\tcomma-separated list of languages to include (help to list)\n"
	"  -w      \twarn about unsupported font features\n";

int main(int argc, char *argv[])
{
	int afm = 1;
	int res = 720;
	int spc = 0;
	int kmin = 0;
	int bbox = 0;
	int warn = 0;
	int ligs = 1;
	int pos = 1;
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
		case 'l':
			ligs = 0;
			break;
		case 'L':
			trfn_langs = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 'n':
			pos = 0;
			break;
		case 'o':
			afm = 0;
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
	trfn_init(res, spc, kmin, bbox, ligs, pos);
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
