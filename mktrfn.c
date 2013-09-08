/*
 * mktrfn - produce troff font descriptions
 *
 * Copyright (C) 2012-2013 Ali Gholami Rudi <ali at rudi dot ir>
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

static void otfdump_read(void)
{
	char cmd[TOKLEN];
	char name[TOKLEN];
	char ch[TOKLEN];
	char c1[TOKLEN], c2[TOKLEN];
	char wid[TOKLEN];
	while (scanf("%s", cmd) == 1) {
		if (!strcmp("name", cmd)) {
			scanf("%s", name);
			trfn_psfont(name);
		}
		if (!strcmp("char", cmd)) {
			scanf("%s width %s", ch, wid);
			trfn_char(ch, NULL, atoi(wid), -1);
		}
		if (!strcmp("kernpair", cmd)) {
			scanf("%s %s width %s", c1, c2, wid);
			trfn_kern(c1, c2, atoi(wid));
		}
		if (!strcmp("feature", cmd)) {
			scanf("%s substitution %s %s", name, c1, c2);
		}
	}
}

static void afm_read(void)
{
	char ch[TOKLEN], pos[TOKLEN];
	char c1[TOKLEN], c2[TOKLEN];
	char wid[TOKLEN];
	char ln[1024];
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
		if (sscanf(ln, "C %s ; WX %s ; N %s", pos, wid, ch) == 3)
			trfn_char(ch, pos, atoi(wid), -1);
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
}

static char *usage =
	"Usage: mktrfn [options] <input >output\n"
	"Options:\n"
	"  -o      \tread the output of otfdump for otf and ttf files (default)\n"
	"  -a      \tread an AFM file\n"
	"  -s      \tspecial font\n"
	"  -p name \toverride font postscript name\n"
	"  -t name \tset font troff name\n"
	"  -r res  \tset device resolution (720)\n";

int main(int argc, char *argv[])
{
	int afm = 0;
	int i = 1;
	int res = 720;
	int spc = 0;
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		switch (argv[i][1]) {
		case 'a':
			afm = 1;
			break;
		case 'o':
			afm = 0;
			break;
		case 'r':
			res = atoi(argv[i][2] ? argv[i] + 2 : argv[++i]);
			break;
		case 's':
			spc = 1;
			break;
		case 't':
			trfn_trfont(argv[i][2] ? argv[i] + 2 : argv[++i]);
			break;
		case 'p':
			trfn_psfont(argv[i][2] ? argv[i] + 2 : argv[++i]);
			break;
		default:
			printf("%s", usage);
			return 0;
		}
	}
	trfn_init(res, spc);
	if (afm)
		afm_read();
	else
		otfdump_read();
	trfn_print();
	trfn_done();
	return 0;
}
