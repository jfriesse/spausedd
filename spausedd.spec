%bcond_with systemd
%bcond_with vmguestlib

%define _disable_source_fetch 0

Name: spausedd
Summary: Utility to detect and log scheduler pause
Version: 20210520
Release: 1%{?dist}
License: ISC
URL: https://github.com/jfriesse/spausedd
Source0: https://github.com/jfriesse/%{name}/releases/download/%{version}/%{name}-%{version}.tar.gz

# Systemd for RHEL >= 7 or Fedora >= 15
%if 0%{?rhel} >= 7
%global use_systemd 1
%endif

%if 0%{?fedora} >= 15
%global use_systemd 1
%endif

# VMGuestLib for RHEL 7@x86-64 and Fedora >= 20@x86-64/i386
%if 0%{?rhel} >= 7
%ifarch x86_64
%global use_vmguestlib 1
%endif
%endif

%if 0%{?fedora} >= 20
%ifarch x86_64 %{ix86}
%global use_vmguestlib 1
%endif
%endif

BuildRequires: gcc

%if %{defined use_systemd}
%{?systemd_requires}
BuildRequires: systemd
%else
Requires(post): /sbin/chkconfig
Requires(preun): /sbin/chkconfig
%endif

%if %{defined use_vmguestlib}
BuildRequires: pkgconfig(vmguestlib)
%endif

%description
Utility to detect and log scheduler pause

%prep
%setup -q -n %{name}-%{version}

%build
CFLAGS="${CFLAGS:-%{optflags}}" ; export CFLAGS
make \
%if %{defined use_vmguestlib}
    WITH_VMGUESTLIB=1 \
%else
    WITH_VMGUESTLIB=0 \
%endif
    %{?_smp_mflags}

%install
make DESTDIR="%{buildroot}" PREFIX="%{_prefix}" install

%if %{defined use_systemd}
mkdir -p %{buildroot}/%{_unitdir}
install -m 644 -p init/%{name}.service %{buildroot}/%{_unitdir}
%else
mkdir -p %{buildroot}/%{_initrddir}
install -m 755 -p init/%{name} %{buildroot}/%{_initrddir}
%endif

%clean

%files
%doc AUTHORS COPYING
%{_bindir}/%{name}
%{_mandir}/man8/*
%if %{defined use_systemd}
%{_unitdir}/spausedd.service
%else
%{_initrddir}/spausedd
%endif

%post
%if %{defined use_systemd} && 0%{?systemd_post:1}
%systemd_post spausedd.service
%else
if [ $1 -eq 1 ]; then
    /sbin/chkconfig --add spausedd || :
fi
%endif

%preun
%if %{defined use_systemd} && 0%{?systemd_preun:1}
%systemd_preun spausedd.service
%else
if [ $1 -eq 0 ]; then
    /sbin/service spausedd stop &>/dev/null || :
    /sbin/chkconfig --del spausedd || :
fi
%endif

%postun
%if %{defined use_systemd} && 0%{?systemd_postun:1}
%systemd_postun spausedd.service
%endif

%changelog
* Thu May 20 2021 Jan Friesse <jfriesse@redhat.com> - 20210520-1
- Document cgroup v2 problems

* Tue May 11 2021 Jan Friesse <jfriesse@redhat.com> - 20210511-1
- Support for cgroup v2

* Fri Mar 26 2021 Jan Friesse <jfriesse@redhat.com> - 20210326-1
- Fix possible memory leak
- Check memlock rlimit

* Thu Nov 12 2020 Jan Friesse <jfriesse@redhat.com> - 20201112-1
- Add ability to move process into root cgroup

* Tue Nov 10 2020 Jan Friesse <jfriesse@redhat.com> - 20201110-1
- Fix log_perror
- New release

* Mon Mar 23 2020 Jan Friesse <jfriesse@redhat.com> - 20200323-1
- Fix man page
- New release

* Wed Aug 07 2019 Jan Friesse <jfriesse@redhat.com> - 20190807-1
- Enhance makefile
- New release

* Tue Aug 06 2019 Jan Friesse <jfriesse@redhat.com> - 20190320-2
- Do not set exec permission for service file
- Fix CFLAGS definition

* Wed Mar 20 2019 Jan Friesse <jfriesse@redhat.com> - 20190320-1
- Use license macro in spec file

* Tue Mar 19 2019 Jan Friesse <jfriesse@redhat.com> - 20190319-1
- Add AUTHORS and COPYING
- Fix version number in specfile
- Use install -p to preserve timestamps
- New release

* Mon Mar 18 2019 Jan Friesse <jfriesse@redhat.com> - 20190318-1
- New release

* Wed Mar 21 2018 Jan Friesse <jfriesse@redhat.com> - 20180321-1
- Remove exlusivearch for VMGuestLib.
- Add copr branch with enhanced spec file which tries to automatically
  detect what build options should be used (systemd/vmguestlib).

* Tue Mar 20 2018 Jan Friesse <jfriesse@redhat.com> - 20180320-1
- Add support for VMGuestLib
- Add more examples

* Mon Feb 19 2018 Jan Friesse <jfriesse@redhat.com> - 20180219-1
- Add support for steal time

* Fri Feb 9 2018 Jan Friesse <jfriesse@redhat.com> - 20180209-1
- Initial version
