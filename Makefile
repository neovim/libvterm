CCFLAGS=-Wall -I. -std=c99
LDFLAGS=-lutil

ifeq ($(DEBUG),1)
  CCFLAGS+=-ggdb
endif

CCFLAGS+=$(shell pkg-config --cflags glib-2.0)
LDFLAGS+=$(shell pkg-config --libs   glib-2.0)

CFILES=$(wildcard *.c)
OFILES=$(CFILES:.c=.o)
HFILES=$(wildcard *.h)

DEBUGS=debug-passthrough

all: $(DEBUGS)

debug-%: debug-%.c ecma48.o
	gcc -o $@ $^ $(LDFLAGS)

%.o: %.c $(HFILES)
	gcc -o $@ -c $< $(CCFLAGS)

.PHONY: clean
clean:
	rm -f $(DEBUGS) $(OFILES)
