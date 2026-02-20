CC      = gcc
CFLAGS  = -O2 -Wall -Wextra
LDFLAGS = -lm

all: atis_decoder

atis_decoder: atis_decoder.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f atis_decoder
