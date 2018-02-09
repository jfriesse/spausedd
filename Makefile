CFLAGS += -Wall -Wshadow -Wp,-D_FORTIFY_SOURCE=2 -g -O
LDFLAGS += -lrt
PROGRAM_NAME = spausedd
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
INSTALL_PROGRAM ?= install
VERSION = 20180209

$(PROGRAM_NAME): spausedd.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

all: $(PROGRAM_NAME)

install: $(PROGRAM_NAME)
	test -z "$(DESTDIR)/$(BINDIR)" || mkdir -p "$(DESTDIR)/$(BINDIR)"
	$(INSTALL_PROGRAM) -c $< $(DESTDIR)/$(BINDIR)
	test -z "$(DESTDIR)/$(MANDIR)/man8" || mkdir -p "$(DESTDIR)/$(MANDIR)/man8"
	$(INSTALL_PROGRAM) -c -m 0644 $<.8 $(DESTDIR)/$(MANDIR)/man8

uninstall:
	rm -f $(DESTDIR)/$(BINDIR)/$(PROGRAM_NAME)
	rm -f $(DESTDIR)/$(MANDIR)/man8/$(PROGRAM_NAME).8

$(PROGRAM_NAME)-$(VERSION).tar.gz:
	mkdir -p $(PROGRAM_NAME)-$(VERSION)
	cp -r README.md Makefile *.[ch] $(PROGRAM_NAME).8 $(PROGRAM_NAME).spec init $(PROGRAM_NAME)-$(VERSION)/
	tar -czf $(PROGRAM_NAME)-$(VERSION).tar.gz $(PROGRAM_NAME)-$(VERSION)
	rm -rf $(PROGRAM_NAME)-$(VERSION)

clean:
	rm -f $(PROGRAM_NAME)

dist: $(PROGRAM_NAME)-$(VERSION).tar.gz

rpm: $(PROGRAM_NAME)-$(VERSION).tar.gz
	rpmbuild -ba --define "_sourcedir $(PWD)" $(PROGRAM_NAME).spec
