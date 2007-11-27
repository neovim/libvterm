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

DEBUGS=debug-passthrough debug-gtkterm

all: $(DEBUGS)

debug-%: debug-%.c libecma48.so
	gcc -o $@ $^ $(CCFLAGS) $(LDFLAGS)

debug-gtkterm: debug-gtkterm.c libecma48.so
	gcc -o $@ $^ $(shell pkg-config --cflags --libs gtk+-2.0) $(LDFLAGS)

libecma48.so: ecma48.o parser.o state.o
	gcc -shared -o $@ $^ $(LDFLAGS)

%.o: %.c $(HFILES)
	gcc -fPIC -o $@ -c $< $(CCFLAGS)

.PHONY: clean
clean:
	rm -f $(DEBUGS) $(OFILES) libecma48.so
