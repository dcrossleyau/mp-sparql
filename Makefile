SUBDIRS = src doc
.PHONY: $(SUBDIRS) all
prefix = /usr
datadir = $(prefix)/share
pkgdatadir = $(datadir)/mp-sparql

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean check:
	for d in $(SUBDIRS); do \
		$(MAKE) -C $$d $@; \
	done

install:
	mkdir -p $(DESTDIR)$(pkgdatadir)/bibframe
	cp bibframe/*.xml $(DESTDIR)$(pkgdatadir)/bibframe
	for d in $(SUBDIRS); do \
		$(MAKE) -C $$d $@; \
	done
