%define idmetaversion %(. ./IDMETA; printf $VERSION )
Summary: Metaproxy SPARQL module
Name: mp-sparql
Version: %{idmetaversion}
Release: 1.indexdata
BuildRequires: gcc gcc-c++ pkgconfig
BuildRequires: docbook-style-xsl
BuildRequires: libmetaproxy6-devel >= 1.4.0
License: proprietary
Group: Applications/Internet
Vendor: Index Data ApS <info@indexdata.dk>
Source: mp-sparql-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-root
Packager: Adam Dickmeiss <adam@indexdata.dk>
URL: http://www.indexdata.com/metaproxy

Requires: metaproxy6
Requires: libmetaproxy6

%description
Backend module for querying triplestore stores

%post
if [ -d /usr/lib64/metaproxy6/modules ]; then
       	if [ ! -e /usr/lib64/metaproxy6/modules/metaproxy_filter_sparql.so ]; then
		ln -s /usr/lib64/mp-sparql/metaproxy_filter_sparql.so /usr/lib64/metaproxy6/modules
	fi
fi
if [ -f /var/run/metaproxy.pid ]; then
	/sbin/service metaproxy restart
fi
%preun
if [ $1 = 0 ]; then
	if [ -f /var/run/metaproxy.pid ]; then
		/sbin/service metaproxy restart
	fi
fi

%postun
if [ $1 = 0 ]; then
	rm -f /usr/lib64/metaproxy6/modules/metaproxy_filter_sparql.so
fi

%prep
%setup

%build
make \
	MP_CONFIG=/usr/bin/metaproxy-config

%install
make install DESTDIR=${RPM_BUILD_ROOT} prefix=%{_prefix} libdir=%{_libdir}

%clean
rm -fr ${RPM_BUILD_ROOT}

%files
%defattr(-,root,root)
%{_libdir}/mp-sparql/*
%{_mandir}/man3/sparql.*
%{_datadir}/mp-sparql
