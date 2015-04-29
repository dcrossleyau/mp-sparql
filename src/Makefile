ROOT=..
prefix=/usr
libdir = ${prefix}/lib
VERSION := $(shell . $(ROOT)/IDMETA; echo $$VERSION)

ifeq "${MP_CONFIG}" ""
MP_CONFIG := $(shell if test -x $(ROOT)/../metaproxy/metaproxy-config; then echo $(ROOT)/../metaproxy/metaproxy-config; else echo metaproxy-config; fi)
endif

MP_CFLAGS := $(shell $(MP_CONFIG) --cflags)
MP_LIBS := $(shell $(MP_CONFIG) --libs)
MP_SO := metaproxy_filter_sparql.so

O := filter_sparql.o sparql.o

CXXFLAGS := -DVERSION=\"$(VERSION)\" $(MP_CFLAGS) -fPIC -g -Wall
CFLAGS := -DVERSION=\"$(VERSION)\" $(MP_CFLAGS) -fPIC -g -Wall

all: $(MP_SO)

$(MP_SO): $(O)
	$(CXX) -shared $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(MP_LIBS)

install: $(MP_SO)
	mkdir -p $(DESTDIR)$(libdir)/mp-sparql
	cp $(MP_SO) $(DESTDIR)$(libdir)/mp-sparql

test_sparql: test_sparql.o sparql.o
	$(CC) $(CFLAGS) $^ -o $@ $(MP_LIBS)

$(O): sparql.h

check: test_sparql
	./test_sparql

clean:
	rm -f *.o $(MP_SO) test_sparql
