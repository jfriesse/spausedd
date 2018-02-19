%bcond_with systemd

Name: spausedd
Summary: Utility to detect and log scheduler pause
Version: 20180219
Release: 1%{?dist}
License: ISC
URL: https://github.com/jfriesse/spausedd
Source0: https://github.com/jfriesse/%{name}/releases/download/%{version}/%{name}-%{version}.tar.gz

%if %{with systemd}
%{?systemd_requires}
BuildRequires: systemd
%else
Requires(post): /sbin/chkconfig
Requires(preun): /sbin/chkconfig
%endif

%description
Utility to detect and log scheduler pause

%prep
%setup -q -n %{name}-%{version}

%build
make %{?_smp_mflags} CFLAGS="%{optflags}"

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
* Mon Feb 19 2018 Jan Friesse <jfriesse@redhat.com> - 20180219-1
- Add support for steal time

* Fri Feb 9 2018 Jan Friesse <jfriesse@redhat.com> - 20180209-1
- Initial version
