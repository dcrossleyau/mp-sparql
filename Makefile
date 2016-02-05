SUBDIRS = src doc
.PHONY: $(SUBDIRS) all
prefix = /usr
datadir = $(prefix)/share
pkgdatadir = $(datadir)/mp-sparql

CDEP=doc/common/common.ent

all: $(SUBDIRS)

$(SUBDIRS): $(CDEP)
	$(MAKE) -C $@

clean check: $(CDEP)
	for d in $(SUBDIRS); do \
		$(MAKE) -C $$d $@; \
	done

dist:
	./mkdist.sh

install:
	mkdir -p $(DESTDIR)$(pkgdatadir)/bibframe
	cp bibframe/cql2pqf.txt bibframe/*.xml $(DESTDIR)$(pkgdatadir)/bibframe
	if test -d /usr/lib64 ; then \
		sed "s@<dlpath.*dlpath>@<dlpath>$(prefix)/lib64/metaproxy6/modules</dlpath>@" <bibframe/config-sparql.xml >$(DESTDIR)$(pkgdatadir)/bibframe/config-sparql.xml; \
	else \
		sed "s@<dlpath.*dlpath>@<dlpath>$(prefix)/lib/metaproxy6/modules</dlpath>@" <bibframe/config-sparql.xml >$(DESTDIR)$(pkgdatadir)/bibframe/config-sparql.xml; \
	fi
	for d in $(SUBDIRS); do \
		$(MAKE) -C $$d $@; \
	done

$(CDEP):
	git submodule init
	git submodule update
