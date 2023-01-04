Name: sps
Version: 3.3
Release: 1%{?dist}
Summary: The Slurm Profiling Service

License: Copyright University of Oxford
Source0: %{name}-%{version}.tar.gz
BuildRoot: %(mktemp -ud %{_tmppath}/%{name}-%{version}-XXXXXX)

BuildRequires: make
BuildRequires: gcc
BuildRequires: gcc-c++
BuildRequires: slurm-devel
Requires: gnuplot

%description
The Slurm (or Simple) Profiling Service sps is a lightweight job profiler which bridges the gap between numerical job stats and full-blown application profiling.

%global debug_package %{nil}

%prep
%setup -q

%build
make all

%install
install --directory $RPM_BUILD_ROOT/usr/share/doc/sps
install --directory $RPM_BUILD_ROOT/usr/lib64/slurm
install --directory $RPM_BUILD_ROOT/usr/bin
install --directory $RPM_BUILD_ROOT/etc/slurm

install -m 644 LICENSE $RPM_BUILD_ROOT/usr/share/doc/sps
install -m 644 README.md $RPM_BUILD_ROOT/usr/share/doc/sps
install -m 755 ckill $RPM_BUILD_ROOT/usr/bin
install -m 755 launch_sps.so $RPM_BUILD_ROOT/usr/lib64/slurm
install -m 755 process_sps.sh $RPM_BUILD_ROOT/etc/slurm
install -m 755 sps $RPM_BUILD_ROOT/usr/bin
install -m 755 sps-plot $RPM_BUILD_ROOT/usr/bin
install -m 755 sps-stop $RPM_BUILD_ROOT/usr/bin
install -m 755 sps-sum $RPM_BUILD_ROOT/usr/bin

%clean
rm -rf $RPM_BUILD_ROOT

%files
/usr/share/doc/sps/LICENSE
/usr/share/doc/sps/README.md
/usr/lib64/slurm/launch_sps.so
/usr/bin/ckill
/usr/bin/sps
/usr/bin/sps-plot
/usr/bin/sps-stop
/usr/bin/sps-sum
/etc/slurm/process_sps.sh

%changelog
