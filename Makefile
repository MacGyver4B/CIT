CIT_CFLAGS = -std=c99 -pipe -O2 -Wall -Wextra -pedantic -Wno-error=date-time

ALL_TARGETS = cit

ifneq ($(ENABLE_MAN),)
ALL_TARGETS += man
endif

ifneq ($(ENABLE_WERROR),)
CIT_CFLAGS += -Werror
endif

override CFLAGS := $(CIT_CFLAGS) $(CFLAGS)

prefix ?= /usr/local
bindir ?= $(prefix)/bin
mandir ?= $(prefix)/share/man

INSTALL ?= install

VERSION := $(shell grep 'define VERSION' cit.c | cut -d\" -f2)

CIT_SRC := Makefile transwarp\ v0.84.prg transwarp\ v0.86.prg $(wildcard *.c *.h *.in LICENSE* README* fatfs/*)

all: $(ALL_TARGETS)

CIT_OBJS = cit.o image.o alloc.o copy.o rel.o config.o tools.o petscii.o gcr.o transwarp.o raw.o undelete.o sd.o textconv.o basic.o util.o
FATFS_OBJS = fatfs/ff.o fatfs/ffunicode.o

$(CIT_OBJS): cit.h
sd.o: fatfs/ff.h fatfs/ffconf.h fatfs/diskio.h

# FatFs as it is, without the warnings of our own code
$(FATFS_OBJS): fatfs/ff.h fatfs/ffconf.h
fatfs/%.o: fatfs/%.c
	$(CC) -std=c99 -pipe -O2 -c -o $@ $<

cit: $(CIT_OBJS) $(FATFS_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(CIT_OBJS) $(FATFS_OBJS) $(LDLIBS)

cit.1.txt: cit.1.txt.in
	sed -e "s/@@VERSION@@/$(VERSION)/g" < cit.1.txt.in > cit.1.txt

cit.1: cit.1.txt
	a2x -d manpage -f manpage cit.1.txt

man: cit.1

test_cit.o: test_cit.c

test_cit: test_cit.o

# the tests must not see a cit.ini of the user
NOCONFIG = XDG_CONFIG_HOME=/nonexistent HOME=/nonexistent APPDATA=/nonexistent

check: cit test_cit
	$(NOCONFIG) ./test_cit ./cit
	$(NOCONFIG) sh tests/formats.sh ./cit

test: check

# byte for byte comparison with an older state: make compare REF=<git ref>
REF ?= cc1541-4.2
compare: cit
	$(NOCONFIG) ./tests/compare.sh $(REF) ./cit

cit_asan: $(CIT_OBJS:.o=.c) $(FATFS_OBJS:.o=.c) cit.h
	$(CC) $(CFLAGS) -g -fsanitize=address -fsanitize=undefined -o $@ $(CIT_OBJS:.o=.c) $(FATFS_OBJS:.o=.c)

asantest: cit_asan test_cit
	./test_cit ./cit_asan

install: all
	$(INSTALL) -Dpm 0755 ./cit $(DESTDIR)$(bindir)/cit
ifneq ($(ENABLE_MAN),)
	$(INSTALL) -Dpm 0644 ./cit.1 $(DESTDIR)$(mandir)/man1/cit.1
endif

cit-$(VERSION).tar: $(CIT_SRC)
	rm -rf cit-$(VERSION)/ *~ README.md.T
	$(eval $@_TMP := $(shell mktemp -d))
	mkdir -p $($@_TMP)/cit-$(VERSION)
	cp -a $(CIT_SRC) $($@_TMP)/cit-$(VERSION)/
	chmod 0644 $($@_TMP)/cit-$(VERSION)/*
	tar cf cit-$(VERSION).tar -C$($@_TMP) cit-$(VERSION)/
	rm -rf $($@_TMP)

cit-$(VERSION).tar.bz2: cit-$(VERSION).tar
	bzip2 -9cz < cit-$(VERSION).tar > cit-$(VERSION).tar.bz2

cit-$(VERSION).tar.gz: cit-$(VERSION).tar
	gzip -9c < cit-$(VERSION).tar > cit-$(VERSION).tar.gz

cit-$(VERSION).tar.xz: cit-$(VERSION).tar
	xz -ce < cit-$(VERSION).tar > cit-$(VERSION).tar.xz

cit-$(VERSION).zip: $(CIT_SRC)
	rm -rf cit-$(VERSION)/ *~ README.md.T
	$(eval $@_SRC := $(shell pwd))
	$(eval $@_TMP := $(shell mktemp -d))
	mkdir -p $($@_TMP)/cit-$(VERSION)
	cp -a $(CIT_SRC) $($@_TMP)/cit-$(VERSION)/
	chmod 0644 $($@_TMP)/cit-$(VERSION)/*
	cd $($@_TMP) && \
	zip -9qr $($@_SRC)/cit-$(VERSION).zip cit-$(VERSION)/
	rm -rf $($@_TMP)

dist-bz2: cit-$(VERSION).tar.bz2
dist-gz:  cit-$(VERSION).tar.gz
dist-xz:  cit-$(VERSION).tar.xz
dist-zip: cit-$(VERSION).zip
dist-all: dist-bz2 dist-gz dist-xz dist-zip

dist: dist-gz dist-zip

dist-check: dist
	$(eval $@_TMP := $(shell mktemp -d))
	mkdir -p $($@_TMP)
	tar xf cit-$(VERSION).tar.gz -C $($@_TMP)
	$(MAKE) -C $($@_TMP)/cit-$(VERSION)/ all check
	rm -rf $($@_TMP)

codestyle: $(CIT_OBJS:.o=.c) cit.h test_cit.c
	astyle --style=kr -n -s -z2 $^

wrap: cit.1.txt.in LICENSE.txt README.md
	for f in $^; do \
	  fold -s -w 76 < $$f | perl -pe 's/[\t\040]+$$//' > $$f.T; \
	  mv -f $$f.T $$f; \
	done

clean:
	rm -rf cit-$(VERSION)/ *~ README.md.T *.o fatfs/*.o *.orig cit cit_asan test_cit cit-$(VERSION).* cit.1 cit.1.txt

.PHONY: all check clean compare codestyle dist dist-all dist-bz2 dist-check dist-gz dist-xz dist-zip install man test wrap

.EXPORT_ALL_VARIABLES:
