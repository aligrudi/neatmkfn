CC = cc
CFLAGS = -O2 -Wall
LDFLAGS =

OBJS = mkfn.o trfn.o sbuf.o tab.o afm.o otf.o

all: mkfn
%.o: %.c
	$(CC) -c $(CFLAGS) $<
mkfn: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
clean:
	rm -f *.o mkfn
