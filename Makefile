CCFLAGS=-Wall -I. -std=c99
LDFLAGS=-lutil

ifeq ($(DEBUG),1)
  CCFLAGS+=-ggdb -DDEBUG
endif

CCFLAGS+=$(shell pkg-config --cflags glib-2.0)
LDFLAGS+=$(shell pkg-config --libs   glib-2.0)

CFILES=$(wildcard *.c)
OFILES=$(CFILES:.c=.o)
HFILES=$(wildcard *.h)

TEST_CFILES=$(wildcard t/*.c)
TEST_OFILES=$(TEST_CFILES:.c=.o)

DEBUGS=debug-passthrough debug-pangoterm

all: $(DEBUGS)

debug-%: debug-%.c libecma48.so
	gcc -o $@ $^ $(CCFLAGS) $(LDFLAGS)

debug-pangoterm: debug-pangoterm.c libecma48.so
	gcc -o $@ $^ $(CCFLAGS) $(shell pkg-config --cflags --libs gtk+-2.0) $(LDFLAGS)

libecma48.so: ecma48.o parser.o state.o input.o pen.o mode.o
	gcc -shared -o $@ $^ $(LDFLAGS)

%.o: %.c $(HFILES)
	gcc -fPIC -o $@ -c $< $(CCFLAGS)

t/test: libecma48.so $(TEST_OFILES)
	gcc -o $@ $^ $(CCFLAGS) $(LDFLAGS) -lcunit

.PHONY: test
test: libecma48.so t/test
	LD_LIBRARY_PATH=. t/test

.PHONY: clean
clean:
	rm -f $(DEBUGS) $(OFILES) libecma48.so
