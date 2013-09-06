/* variable length string buffer */
struct sbuf {
	char *s;		/* allocated buffer */
	int sz;			/* buffer size */
	int n;			/* length of the string stored in s */
};

void sbuf_init(struct sbuf *sbuf);
void sbuf_done(struct sbuf *sbuf);
char *sbuf_buf(struct sbuf *sbuf);
void sbuf_printf(struct sbuf *sbuf, char *s, ...);
