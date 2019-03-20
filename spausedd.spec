%bcond_with systemd
%bcond_with vmguestlib

Name: spausedd
Summary: Utility to detect and log scheduler pause
Version: 20190320
Release: 1%{?dist}
License: ISC
URL: https://github.com/jfriesse/spausedd
Source0: https://github.com/jfriesse/%{name}/releases/download/%{version}/%{name}-%{version}.tar.gz

# VMGuestLib exists only for x86 architectures
%if %{with vmguestlib}
%ifarch %{ix86} x86_64
%global use_vmguestlib 1
%endif
%endif

BuildRequires: gcc

%if %{with systemd}
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
%set_build_flags
make \
%if %{defined use_vmguestlib}
    WITH_VMGUESTLIB=1 \
%else
    WITH_VMGUESTLIB=0 \
%endif
    %{?_smp_mflags}

%install
make DESTDIR="%{buildroot}" PREFIX="%{_prefix}" install

%if %{with systemd}
mkdir -p %{buildroot}/%{_unitdir}
install -m 755 -p init/%{name}.service %{buildroot}/%{_unitdir}
%else
mkdir -p %{buildroot}/%{_initrddir}
install -m 755 -p init/%{name} %{buildroot}/%{_initrddir}
%endif

%clean

%files
%doc AUTHORS
%license COPYING
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
* Wed Mar 20 2019 Jan Friesse <jfriesse@redhat.com> - 20190320-1
- Use license macro in spec file

* Tue Mar 19 2019 Jan Friesse <jfriesse@redhat.com> - 20190319-1
- Add AUTHORS and COPYING
- Fix version number in specfile
- Use install -p to preserve timestamps
- Use set_build_flags macro

* Mon Mar 18 2019 Jan Friesse <jfriesse@redhat.com> - 20190318-1
- Require VMGuestLib only on x86 and x86_64

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
