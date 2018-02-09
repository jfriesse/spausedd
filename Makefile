CFLAGS += -Wall -Wshadow -Wp,-D_FORTIFY_SOURCE=2 -g -O
LDFLAGS += -lrt
PROGRAM_NAME = spausedd
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
INSTALL_PROGRAM ?= install

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

clean:
	rm -f $(PROGRAM_NAME)
