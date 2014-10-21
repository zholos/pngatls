CFLAGS += -std=c99 -O2 -I/usr/local/include
LDFLAGS += -L/usr/local/lib -lpng -lz -lm

pngatls: pngatls.c

clean:
	rm -f pngatls
