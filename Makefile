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

CIT_SRC := Makefile transwarp\ v0.84.prg transwarp\ v0.86.prg $(wildcard *.c *.h *.in LICENSE* README*)

all: $(ALL_TARGETS)

CIT_OBJS = cit.o image.o petscii.o gcr.o transwarp.o g64.o undelete.o

$(CIT_OBJS): cit.h

cit: $(CIT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(CIT_OBJS) $(LDLIBS)

cit.1.txt: cit.1.txt.in
	sed -e "s/@@VERSION@@/$(VERSION)/g" < cit.1.txt.in > cit.1.txt

cit.1: cit.1.txt
	a2x -d manpage -f manpage cit.1.txt

man: cit.1

test_cit.o: test_cit.c

test_cit: test_cit.o

check: cit test_cit
	./test_cit ./cit

test: check

cit_asan: $(CIT_OBJS:.o=.c) cit.h
	$(CC) $(CFLAGS) -g -fsanitize=address -fsanitize=undefined -o $@ $(CIT_OBJS:.o=.c)

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
	rm -rf cit-$(VERSION)/ *~ README.md.T *.o *.orig cit cit_asan test_cit cit-$(VERSION).* cit.1 cit.1.txt

.PHONY: all check clean codestyle dist dist-all dist-bz2 dist-check dist-gz dist-xz dist-zip install man test wrap

.EXPORT_ALL_VARIABLES:
