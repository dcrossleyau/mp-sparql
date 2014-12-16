SUBDIRS = src
.PHONY: $(SUBDIRS) all

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean install:
	for d in $(SUBDIRS); do \
		$(MAKE) -C $$d $@; \
	done
