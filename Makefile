CFLAGS ?= -Wp,-D_FORTIFY_SOURCE=2 -g -O2
CFLAGS_ADD = -Wall -Wshadow
LDFLAGS_ADD = -lrt
PROGRAM_NAME = spausedd
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
INSTALL_PROGRAM ?= install
VERSION = 20210719

ifeq ($(or $(WITH_VMGUESTLIB), $(shell pkg-config --exists vmguestlib && echo "1" || echo "0")), 1)
VMGUESTLIB_CFLAGS += $(shell pkg-config vmguestlib --cflags) -DHAVE_VMGUESTLIB
VMGUESTLIB_LDFLAGS += $(shell pkg-config vmguestlib --libs)
endif

$(PROGRAM_NAME): spausedd.c
	$(CC) $(CFLAGS_ADD) $(VMGUESTLIB_CFLAGS) $(CFLAGS) $< $(LDFLAGS_ADD) $(VMGUESTLIB_LDFLAGS) $(LDFLAGS) -o $@

all: $(PROGRAM_NAME)

install: $(PROGRAM_NAME)
	test -z "$(DESTDIR)/$(BINDIR)" || mkdir -p "$(DESTDIR)/$(BINDIR)"
	$(INSTALL_PROGRAM) -p -c $< $(DESTDIR)/$(BINDIR)
	test -z "$(DESTDIR)/$(MANDIR)/man8" || mkdir -p "$(DESTDIR)/$(MANDIR)/man8"
	$(INSTALL_PROGRAM) -p -c -m 0644 $<.8 $(DESTDIR)/$(MANDIR)/man8

uninstall:
	rm -f $(DESTDIR)/$(BINDIR)/$(PROGRAM_NAME)
	rm -f $(DESTDIR)/$(MANDIR)/man8/$(PROGRAM_NAME).8

$(PROGRAM_NAME)-$(VERSION).tar.gz:
	mkdir -p $(PROGRAM_NAME)-$(VERSION)
	cp -r AUTHORS COPYING README.md Makefile *.[ch] $(PROGRAM_NAME).8 $(PROGRAM_NAME).spec init $(PROGRAM_NAME)-$(VERSION)/
	tar -czf $(PROGRAM_NAME)-$(VERSION).tar.gz $(PROGRAM_NAME)-$(VERSION)
	rm -rf $(PROGRAM_NAME)-$(VERSION)

clean:
	rm -f $(PROGRAM_NAME) $(PROGRAM_NAME)-*.tar.gz

dist: $(PROGRAM_NAME)-$(VERSION).tar.gz

rpm: $(PROGRAM_NAME)-$(VERSION).tar.gz
	rpmbuild -ba --define "_sourcedir $(PWD)" $(PROGRAM_NAME).spec

rpm-with-all: $(PROGRAM_NAME)-$(VERSION).tar.gz
	rpmbuild -ba --define "_sourcedir $(PWD)" --with systemd --with vmguestlib $(PROGRAM_NAME).spec
