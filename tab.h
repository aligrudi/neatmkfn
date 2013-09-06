struct tab *tab_alloc(int sz);
void tab_free(struct tab *tab);
void tab_put(struct tab *tab, char *k, void *v);
void *tab_get(struct tab *tab, char *k);
