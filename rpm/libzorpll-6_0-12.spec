Name:			libzorpll-6_0-12
Version:		6.0.12.0
Release:		1%{?dist}
URL:			https://balasys.github.io/zorp/
%if 0%{?fedora}
%else
Vendor:			BalaSys IT
Packager:		BalaSys Development Team <devel@balasys.hu>
%endif

Source:			libzorpll_%{version}.tar.gz
Summary:		Low level library functions for Zorp
License:		GPL-2.0
Group:			System/Libraries
%if 0%{?fedora} || 0%{?rhel} || 0%{?centos}
BuildRequires:		openssl-devel
%else
BuildRequires:		libopenssl-devel
%endif
BuildRequires:		glib2-devel
BuildRequires:		libcap-devel
BuildRequires:		zlib-devel
BuildRequires:		binutils-devel
BuildRequires:		automake
BuildRequires:		autoconf
BuildRequires:		autoconf-archive
BuildRequires:		libtool
BuildRequires:		gcc-c++
BuildRequires:		boost-devel

BuildRoot:		%{_tmppath}/%{name}-%{version}-build

%description
Low level library functions for Zorp

Zorp is a new generation firewall. It is essentially a transparent proxy
firewall, with strict protocol analyzing proxies, a modular architecture,
and fine-grained control over the mediated traffic. Configuration decisions
are scriptable with the Python based configuration language.

This package contains low level library functions needed by Zorp and
associated programs.

%prep
%setup -q -n libzorpll

%build
autoreconf -if
%configure --disable-werror
make %{?_smp_mflags}

%install
%make_install

%check
make check %{?_smp_mflags}

%post
ldconfig

%postun
ldconfig

%files
%defattr(-,root,root)
%{_libdir}/libzorpll*.so.*
%{_libexecdir}/failure_notify*

%package devel
Summary:		Headers for libzorpll
Group:			Development/Libraries
Requires:		zlib-devel
Requires:		glibc-devel
Requires:		libcap-devel
Requires:		glib2-devel
Requires:		binutils-devel
Requires:		%{name} = %{version}

%if 0%{?fedora} || 0%{?rhel} || 0%{?centos}
Requires:		openssl-devel
%else
Requires:		libopenssl-devel
%endif

%description devel
Low level library functions for Zorp, development files

Zorp is a new generation firewall. It is essentially a transparent proxy
firewall, with strict protocol analyzing proxies, a modular architecture,
and fine-grained control over the mediated traffic. Configuration decisions
are scriptable with the Python based configuration language.

This package contains the development files necessary to create programs
based on libzorpll.

%files devel
%defattr(-,root,root)
%{_libdir}/pkgconfig/zorpll-6.0.pc
%{_libdir}/libzorpll-6.0.a
%{_libdir}/libzorpll-6.0*.la
%{_libdir}/libzorpll-6.0*.so
%dir %{_includedir}/zorp-6.0
%dir %{_includedir}/zorp-6.0/zorpll
%{_includedir}/zorp-6.0/zorpll/*.h

%changelog
* Thu May 10 2018 Balasys Development Team <devel@balasys.hu> - 6.0.12.0-1
- Version bump
* Wed Sep 13 2017 Balasys Development Team <devel@balasys.hu> - 6.0.11.0-1
- Version bump
* Fri Nov 25 2016 Balasys Development Team <devel@balasys.hu> - 6.0.10.0-1
- Version bump
* Sun Sep 25 2016 Balasys Development Team <devel@balasys.hu> - 6.0.9.0-1
- Version bump
* Sun Feb 21 2016 Balasys Development Team <devel@balasys.hu> - 6.0.8.0-1
- Version bump
* Sun Feb 15 2015 BalaBit Development Team <devel@balabit.hu> - 6.0.0.0-1
- Version bump
