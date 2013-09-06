CC = cc
CFLAGS = -O2 -Wall
LDFLAGS =

all: mktrfn
%.o: %.c
	$(CC) -c $(CFLAGS) $<
mktrfn: mktrfn.o trfn.o sbuf.o tab.o
	$(CC) -o $@ $^ $(LDFLAGS)
clean:
	rm -f *.o mktrfn
