CCFLAGS=-Wall -Iinclude -std=c99
LDFLAGS=-lutil

ifeq ($(DEBUG),1)
  CCFLAGS+=-ggdb -DDEBUG
endif

ifeq ($(PROFILE),1)
  CCFLAGS+=-pg
  LDFLAGS+=-pg
endif

CCFLAGS+=$(shell pkg-config --cflags glib-2.0)
LDFLAGS+=$(shell pkg-config --libs   glib-2.0)

CFILES=$(wildcard src/*.c)
OFILES=$(CFILES:.c=.o)
HFILES=$(wildcard include/*.h)

HFILES_INT=$(wildcard src/*.h) $(HFILES)

LIBPIECES=vterm parser encoding state input pen unicode

all: pangoterm

pangoterm: pangoterm.c libvterm.so
	gcc -o $@ $^ $(CCFLAGS) $(shell pkg-config --cflags --libs gtk+-2.0) $(LDFLAGS)

libvterm.so: $(addprefix src/, $(addsuffix .o, $(LIBPIECES)))
	gcc -shared -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c $(HFILES_INT)
	gcc -fPIC -o $@ -c $< $(CCFLAGS)

# Need first to cancel the implict rule
%.o: %.c

t/%.o: t/%.c t/%.inc $(HFILES)
	gcc -c -o $@ $< $(CCFLAGS)

t/harness: t/harness.c $(HFILES) libvterm.so
	gcc -o $@ $< $(CCFLAGS) libvterm.so

.PHONY: test
test: libvterm.so t/harness
	for T in $(wildcard t/[0-9]*.test); do echo "** $$T **"; perl t/run-test.pl $$T || exit 1; done

.PHONY: clean
clean:
	rm -f $(DEBUGS) $(OFILES) libvterm.so
