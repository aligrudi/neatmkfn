/* A Dictionary */
#include <stdlib.h>
#include <string.h>
#include "mkfn.h"

struct tab {
	char **keys;
	void **vals;
	int n;
	int *next;
	int head[256];
};

struct tab *tab_alloc(int sz)
{
	struct tab *tab = malloc(sizeof(*tab));
	int i;
	memset(tab, 0, sizeof(*tab));
	tab->keys = malloc(sz * sizeof(tab->keys[0]));
	tab->vals = malloc(sz * sizeof(tab->vals[0]));
	tab->next = malloc(sz * sizeof(tab->next[0]));
	for (i = 0; i < 256; i++)
		tab->head[i] = -1;
	return tab;
}

void tab_free(struct tab *tab)
{
	free(tab->keys);
	free(tab->vals);
	free(tab->next);
	free(tab);
}

void tab_put(struct tab *tab, char *k, void *v)
{
	tab->keys[tab->n] = k;
	tab->vals[tab->n] = v;
	tab->next[tab->n] = tab->head[(unsigned char) k[0]];
	tab->head[(unsigned char) k[0]] = tab->n;
	tab->n++;
}

void *tab_get(struct tab *tab, char *k)
{
	int i = tab->head[(unsigned char) k[0]];
	while (i >= 0) {
		if (k[1] == tab->keys[i][1] && !strcmp(k, tab->keys[i]))
			return tab->vals[i];
		i = tab->next[i];
	}
	return NULL;
}
