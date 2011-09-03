ifeq ($(shell uname),Darwin)
    LIBTOOL ?= glibtool
else
    LIBTOOL ?= libtool
endif

ifneq ($(VERBOSE),1)
    LIBTOOL +=--quiet
endif

CFLAGS  +=-Wall -Iinclude -std=c99
LDFLAGS +=-lutil

ifeq ($(DEBUG),1)
  CFLAGS +=-ggdb -DDEBUG
endif

ifeq ($(PROFILE),1)
  CFLAGS +=-pg
  LDFLAGS+=-pg
endif

CFLAGS +=$(shell pkg-config --cflags glib-2.0)
LDFLAGS+=$(shell pkg-config --libs   glib-2.0)

CFILES=$(wildcard src/*.c)
HFILES=$(wildcard include/*.h)
OBJECTS=$(CFILES:.c=.lo)
LIBRARY=libvterm.la

TBLFILES=$(wildcard src/encoding/*.tbl)
INCFILES=$(TBLFILES:.tbl=.inc)

HFILES_INT=$(wildcard src/*.h) $(HFILES)

VERSION_CURRENT=0
VERSION_REVISION=0
VERSION_AGE=0

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
LIBDIR=$(PREFIX)/lib
INCDIR=$(PREFIX)/include
MANDIR=$(PREFIX)/share/man
MAN3DIR=$(MANDIR)/man3

all: pangoterm

pango%: pango%.c $(LIBRARY)
	@echo LINK $@
	@$(LIBTOOL) --mode=link --tag=CC $(CC) $(CFLAGS) -o $@ $^ $(shell pkg-config --cflags --libs gtk+-2.0) $(LDFLAGS)

$(LIBRARY): $(OBJECTS)
	@echo LINK $@
	@$(LIBTOOL) --mode=link --tag=CC $(CC) -rpath $(LIBDIR) -version-info $(VERSION_CURRENT):$(VERSION_REVISION):$(VERSION_AGE) -o $@ $^

src/%.lo: src/%.c $(HFILES_INT)
	@echo CC $<
	@$(LIBTOOL) --mode=compile --tag=CC $(CC) $(CFLAGS) -o $@ -c $<

src/encoding/%.inc: src/encoding/%.tbl
	@echo TBL $<
	@perl -C tbl2inc_c.pl $< >$@

src/encoding.lo: $(INCFILES)

t/harness.lo: t/harness.c $(HFILES)
	@echo CC $<
	@$(LIBTOOL) --mode=compile --tag=CC $(CC) $(CFLAGS) -o $@ -c $<

t/harness: t/harness.lo $(LIBRARY)
	@echo LINK $@
	@$(LIBTOOL) --mode=link --tag=CC $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: test
test: $(LIBRARY) t/harness
	for T in $(wildcard t/[0-9]*.test); do echo "** $$T **"; perl t/run-test.pl $$T || exit 1; done

.PHONY: clean
clean:
	$(LIBTOOL) --mode=clean rm -f $(OBJECTS)
	$(LIBTOOL) --mode=clean rm -f t/harness.lo t/harness
	$(LIBTOOL) --mode=clean rm -f pangoterm
	$(LIBTOOL) --mode=clean rm -f $(LIBRARY)

.PHONY: install
install: install-inc install-lib install-bin

install-inc:
	install -d $(DESTDIR)$(INCDIR)
	install -m644 $(HFILES) $(DESTDIR)$(INCDIR)
	install -d $(DESTDIR)$(LIBDIR)/pkgconfig
	sed "s,@PREFIX@,$(PREFIX)," <vterm.pc.in >$(DESTDIR)$(LIBDIR)/pkgconfig/vterm.pc

install-lib:
	install -d $(DESTDIR)$(LIBDIR)
	$(LIBTOOL) --mode=install cp $(LIBRARY) $(DESTDIR)$(LIBDIR)/libvterm.lao

install-bin: pangoterm
	install -d $(DESTDIR)$(BINDIR)
	$(LIBTOOL) --mode=install cp pangoterm $(DESTDIR)$(BINDIR)/pangoterm
