CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2 -fopenmp
LDFLAGS = -lpng -fopenmp
TARGETS = png_to_jasc quantize_png

all: $(TARGETS)

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c $< -o $@

png_to_jasc: png_to_jasc.c utils.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

quantize_png: quantize_png.c utils.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS) utils.o

.PHONY: all clean
