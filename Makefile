CC = cc
CFLAGS = -O2 -Wall
LDFLAGS =

all: mkfn
%.o: %.c
	$(CC) -c $(CFLAGS) $<
mkfn: mkfn.o trfn.o sbuf.o tab.o otf.o
	$(CC) -o $@ $^ $(LDFLAGS)
clean:
	rm -f *.o mkfn
