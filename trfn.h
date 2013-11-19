void trfn_init(int res, int special, int kmin);
void trfn_done(void);
void trfn_trfont(char *name);
void trfn_psfont(char *fontname);
void trfn_print(void);
void trfn_char(char *c, char *n, int wid, int typ);
void trfn_kern(char *c1, char *c2, int x);
void trfn_sub(char *c1, char *c2);
