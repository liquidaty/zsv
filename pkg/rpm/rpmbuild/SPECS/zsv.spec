Name:           zsv
Version:        1.1.0
Release:        1%{?dist}
Summary:        Tabular data Swiss-army knife CLI
Group:          Applications/Utilities

License:        MIT
URL:            https://github.com/liquidaty/zsv
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz
Source1:        zsv.1

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  jq-devel
BuildRequires:  ncurses-devel
BuildRequires:  pcre2-devel

Requires:       ncurses

%description
zsv is a fast and extensible CLI utility for CSV data.
It achieves high performance using SIMD operations,
efficient memory use and other optimization techniques,
and can also parse generic-delimited and fixed-width
formats, as well as multi-row-span headers.

%package devel
Summary:        C headers and static library for zsv
Group:          Development/Libraries
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description devel
The zsv-devel package contains the headers and library.

%prep
%autosetup

%build
%configure
make build VERSION=%{version}

%install
%make_install

mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_includedir}/zsv
mkdir -p %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_mandir}/man1

# Binary
install -p -m 755 /usr/bin/zsv %{buildroot}%{_bindir}/

# Headers
install -p -m 644 /usr/include/zsv.h %{buildroot}%{_includedir}/
cp -pr /usr/include/zsv/* %{buildroot}%{_includedir}/zsv/

# Library
# NOTE: zsv uses `lib`, not `lib64` even on 64-bit systems
install -p -m 644 /usr/lib/libzsv.a %{buildroot}%{_libdir}/

# Manual
install -p -m 644 %{SOURCE1} %{buildroot}%{_mandir}/man1/zsv.1

%check

%files
%{_bindir}/zsv
%{_mandir}/man1/zsv.1.gz
%doc README.md
%license LICENSE

%files devel
%{_includedir}/zsv.h
%dir %{_includedir}/zsv
%{_includedir}/zsv/*
%{_libdir}/libzsv.a
%doc README.md
%license LICENSE

%changelog
* Sun Nov 09 2025 Azeem Sajid <azeem.sajid@gmail.com> - 1.1.0-1
- Initial version of the package
