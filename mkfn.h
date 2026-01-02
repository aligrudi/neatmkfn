/* functions used by afm.c and otf.c */
void mkfn_trfont(char *name);
void mkfn_psfont(char *fontname);
void mkfn_header(char *fontname);
void mkfn_char(char *c, int n, int u, int wid, int llx, int lly, int urx, int ury);
void mkfn_kern(char *c1, char *c2, int x);
int mkfn_font(char *font);
int mkfn_script(char *script, int nscripts);
int mkfn_lang(char *lang, int nlangs);
int mkfn_featrank(char *scrp, char *feat);

/* functions defined in trfn.c and used by mkfn.c */
void trfn_init(void);
void trfn_cdefs(void);
void trfn_header(void);
void trfn_done(void);

/* global variables */
extern int mkfn_res;		/* device resolution */
extern int mkfn_warn;		/* warn about unsupported features */
extern int mkfn_kmin;		/* minimum kerning value */
extern int mkfn_swid;		/* space width */
extern int mkfn_special;	/* special flag */
extern int mkfn_bbox;		/* include bounding box */
extern int mkfn_noligs;		/* suppress ligatures */
extern int mkfn_pos;		/* include glyph positions */
extern int mkfn_byname;		/* always reference glyphs by name */
extern int mkfn_dry;		/* generate no output */

/* variable length string buffer */
struct sbuf *sbuf_make(void);
void sbuf_free(struct sbuf *sb);
char *sbuf_done(struct sbuf *sb);
void sbuf_str(struct sbuf *sbuf, char *s);
void sbuf_mem(struct sbuf *sbuf, char *s, int len);
char *sbuf_buf(struct sbuf *sb);
void sbuf_printf(struct sbuf *sbuf, char *s, ...);

/* dictionary */
struct tab *tab_alloc(int sz);
void tab_free(struct tab *tab);
void tab_put(struct tab *tab, char *k, void *v);
void *tab_get(struct tab *tab, char *k);
