CC=gcc
CFLAGS= -std=c99 -Wall
LDFLAGS= -lm -lSDL2 -lSDL2_image -lenet

all: tanks.c
		$(CC) $(CFLAGS) tanks.c -o tanks $(LDFLAGS)
