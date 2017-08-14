/* variable length string buffer */
struct sbuf *sbuf_make(void);
void sbuf_free(struct sbuf *sb);
char *sbuf_done(struct sbuf *sb);
void sbuf_str(struct sbuf *sbuf, char *s);
void sbuf_mem(struct sbuf *sbuf, char *s, int len);
char *sbuf_buf(struct sbuf *sb);
void sbuf_printf(struct sbuf *sbuf, char *s, ...);
