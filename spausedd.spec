%bcond_with systemd
%bcond_with vmguestlib

%define _disable_source_fetch 0

Name: spausedd
Summary: Utility to detect and log scheduler pause
Version: 20180320
Release: 1%{?dist}
License: ISC
URL: https://github.com/jfriesse/spausedd
Source0: https://github.com/jfriesse/%{name}/releases/download/%{version}/%{name}-%{version}.tar.gz

# Systemd for RHEL >= 7 or Fedora >= 15
%if 0%{?rhel} >= 7
%global with_systemd 1
%endif

%if 0%{?fedora} >= 15
%global with_systemd 1
%endif

# VMGuestLib for RHEL 7@x86-64 and Fedora >= 20@x86-64/i386
%if 0%{?rhel} >= 7
%ifarch x86_64
%global with_vmguestlib 1
%endif
%endif

%if 0%{?fedora} >= 20
%ifarch x86_64 i386
%global with_vmguestlib 1
%endif
%endif

%if %{with systemd}
%{?systemd_requires}
BuildRequires: systemd
%else
Requires(post): /sbin/chkconfig
Requires(preun): /sbin/chkconfig
%endif

%if %{with vmguestlib}
BuildRequires: pkgconfig(vmguestlib)
%endif

%description
Utility to detect and log scheduler pause

%prep
%setup -q -n %{name}-%{version}

%build
make \
%if %{with vmguestlib}
    WITH_VMGUESTLIB=1 \
%else
    WITH_VMGUESTLIB=0 \
%endif
    %{?_smp_mflags} CFLAGS="%{optflags}"

%install
make DESTDIR="%{buildroot}" PREFIX="%{_prefix}" install

%if %{with systemd}
mkdir -p %{buildroot}/%{_unitdir}
install -m 755 init/%{name}.service %{buildroot}/%{_unitdir}
%else
mkdir -p %{buildroot}/%{_initrddir}
install -m 755 init/%{name} %{buildroot}/%{_initrddir}
%endif

%clean

%files
%{_bindir}/%{name}
%{_mandir}/man8/*
%if %{with systemd}
%{_unitdir}/spausedd.service
%else
%{_initrddir}/spausedd
%endif

%post
%if %{with systemd} && 0%{?systemd_post:1}
%systemd_post spausedd.service
%else
if [ $1 -eq 1 ]; then
    /sbin/chkconfig --add spausedd || :
fi
%endif

%preun
%if %{with systemd} && 0%{?systemd_preun:1}
%systemd_preun spausedd.service
%else
if [ $1 -eq 0 ]; then
    /sbin/service spausedd stop &>/dev/null || :
    /sbin/chkconfig --del spausedd || :
fi
%endif

%postun
%if %{with systemd} && 0%{?systemd_postun:1}
%systemd_postun spausedd.service
%endif

%changelog
* Wed Mar 21 2018 Jan Friesse <jfriesse@redhat.com> - 20180321-1
- Remove exlusivearch for VMGuestLib.
- Add copr branch which uses some automagic to decide what to build.

* Tue Mar 20 2018 Jan Friesse <jfriesse@redhat.com> - 20180320-1
- Add support for VMGuestLib
- Add more examples

* Mon Feb 19 2018 Jan Friesse <jfriesse@redhat.com> - 20180219-1
- Add support for steal time

* Fri Feb 9 2018 Jan Friesse <jfriesse@redhat.com> - 20180209-1
- Initial version
