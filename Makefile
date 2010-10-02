
RM = rm
CC = gcc
CFLAGS = -O3 -march=core2 -combine -pipe

ifdef PROFILE
    CFLAGS += -g -pg
endif

all: tdgame

tdgame: tdgame.c
	$(CC) -Wall -std=c99 $(CFLAGS) $(LDFLAGS) -o tdgame tdgame.c

.PHONY: clean
clean:
	$(RM) -f tdgame
