%define nidas_prefix /opt/nidas

# Command line switches:  --with configedit --with autocal --with arinc --with modules
# If not specified, configedit or autocal package will not be built
%bcond_with configedit
%bcond_with autocal
%bcond_with raf
%bcond_with arinc
%bcond_with modules

%if %{with raf}
%define buildraf BUILD_RAF=yes
%else
%define buildraf BUILD_RAF=no
%endif

%if %{with arinc}
%define buildarinc BUILD_ARINC=yes
%else
%define buildarinc BUILD_ARINC=no
%endif

%if %{with modules}
%define buildmodules LINUX_MODULES=yes
%else
%define buildmodules LINUX_MODULES=no
%endif

%define has_systemd 0
%{?systemd_requires: %define has_systemd 1}

Summary: NIDAS: NCAR In-Situ Data Acquistion Software
Name: nidas
Version: %{gitversion}
Release: %{releasenum}%{?dist}
License: GPL
Group: Applications/Engineering
Url: https://github.com/ncareol/nidas
Vendor: UCAR
Source: https://github.com/ncareol/%{name}/archive/master.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires: gcc-c++ xerces-c-devel xmlrpc++ bluez-libs-devel bzip2-devel flex gsl-devel kernel-devel libcap-devel eol_scons
Requires: jsoncpp
BuildRequires: jsoncpp-devel

Requires: yum-utils nidas-min
Obsoletes: nidas-bin <= 1.0
BuildRoot: %{_topdir}/%{name}-%{version}-root
# Allow this package to be relocatable to other places than /opt/nidas
# rpm --relocate /opt/nidas=/usr
Prefix: %{nidas_prefix}
%description
NCAR In-Situ Data Acquistion Software programs

%package min
Summary: Minimal NIDAS run-time configuration.
Group: Applications/Engineering
Obsoletes: nidas <= 1.0, nidas-run <= 1.0
Requires: xerces-c xmlrpc++
%description min
Minimal run-time setup for NIDAS: /etc/ld.so.conf.d/nidas.conf. Useful on systems
that NFS mount %{nidas_prefix}, or do their own builds.

# It works to name /sbin/ldconfig as a post- scriptlet requirement, but it
# does not work to name /sbin/selinuxenabled, even though rpm figures it
# out:
#
# [root@ustar daq]# rpm -q --whatprovides /sbin/selinuxenabled
# libselinux-utils-2.9-5.fc31.x86_64
# [root@ustar daq]# rpm -q --whatprovides /sbin/semanage
# policycoreutils-python-utils-2.9-5.fc31.noarch
# [root@ustar daq]# rpm -q --whatprovides /sbin/restorecon
# policycoreutils-2.9-5.fc31.x86_64
#
# So back to having to use different package names on different releases...
#
%package libs
Summary: NIDAS shareable libraries
Group: Applications/Engineering
Requires: nidas-min
Requires (post): /sbin/ldconfig
Requires (postun): /sbin/ldconfig
%if 0%{?fedora} > 28
Requires (post): libselinux-utils policycoreutils-python-utils policycoreutils
Requires (postun): libselinux-utils policycoreutils-python-utils policycoreutils
%else
%if 0%{?rhel} < 8
Requires (post): policycoreutils-python
Requires (postun): policycoreutils-python
%else
Requires (post): python3-policycoreutils
Requires (postun): python3-policycoreutils
%endif
%endif

Prefix: %{nidas_prefix}
%description libs
NIDAS shareable libraries

%if %{with modules}
%package modules
Summary: NIDAS kernel modules
Group: Applications/Engineering
Requires: nidas
Prefix: %{nidas_prefix}
%description modules
NIDAS kernel modules.
%endif

%if %{with autocal}

%package autocal
Summary: Auto-calibration program, with Qt GUI, for NCAR RAF A2D board
Requires: nidas
Group: Applications/Engineering
Prefix: %{nidas_prefix}
%description autocal
Auto-calibration program, with Qt GUI, for NCAR A2D board.

%endif

%if %{with configedit}

%package configedit
Summary: GUI editor for NIDAS configurations
Requires: nidas
Group: Applications/Engineering
Prefix: %{nidas_prefix}
%description configedit
GUI editor for NIDAS configurations

%endif

%package daq
Summary: Package for doing data acquisition with NIDAS.
# remove dist from release on noarch RPM
Release: %{releasenum}
Requires: nidas-min
Group: Applications/Engineering
BuildArch: noarch
%description daq
Package for doing data acquisition with NIDAS.  Contains some udev rules to
expand permissions on /dev/tty[A-Z]* and /dev/usbtwod*.
Edit /etc/default/nidas-daq to specify the desired user
to run NIDAS real-time data acquisition processes.

%package devel
Summary: Headers, symbolic links and pkg-config for building software which uses NIDAS.
Requires: nidas-libs libcap-devel
Obsoletes: nidas-bin-devel <= 1.0
Group: Applications/Engineering
# Prefix: %%{nidas_prefix}
%description devel
NIDAS C/C++ headers, shareable library links, pkg-config.

%package build
Summary: Package for building NIDAS by hand
# remove dist from release on noarch RPM
Release: %{releasenum}
Group: Applications/Engineering

Requires: gcc-c++ xerces-c-devel xmlrpc++ bluez-libs-devel bzip2-devel flex gsl-devel kernel-devel libcap-devel eol_scons rpm-build
%if 0%{?rhel} < 8
Requires: scons qt-devel
%else
Requires: python3-scons qt5-devel elfutils-libelf-devel
%endif

Obsoletes: nidas-builduser <= 1.2-189
BuildArch: noarch
%description build
Contains software dependencies needed to build NIDAS by hand,
and /etc/default/nidas-build containing the desired user and group owner
of %{nidas_prefix}.

%package buildeol
Summary: Set build user and group to nidas.eol.
# remove dist from release on noarch RPM
Release: %{releasenum}
Group: Applications/Engineering
Requires: nidas-build
BuildArch: noarch
%description buildeol
Sets BUILD_GROUP=eol in /etc/default/nidas-build so that %{nidas_prefix} will be group writable by eol.

%prep
%setup -q -c


%build

%if 0%{?rhel} < 8
scns=scons
%else
scns=scons-3
%endif

cd src
$scns -j 4 --config=force BUILDS=host REPO_TAG=v%{version} %{buildraf} %{buildarinc} %{buildmodules} PREFIX=%{nidas_prefix}
 

%install
rm -rf $RPM_BUILD_ROOT

%if 0%{?rhel} < 8
scns=scons
%else
scns=scons-3
%endif

cd src
$scns -j 4 BUILDS=host PREFIX=${RPM_BUILD_ROOT}%{nidas_prefix} %{buildraf} %{buildarinc} %{buildmodules} REPO_TAG=v%{version} install
cd -

install -d ${RPM_BUILD_ROOT}%{_sysconfdir}/ld.so.conf.d
echo "%{nidas_prefix}/%{_lib}" > $RPM_BUILD_ROOT%{_sysconfdir}/ld.so.conf.d/nidas.conf

install -m 0755 -d $RPM_BUILD_ROOT%{_libdir}/pkgconfig
# scons puts entire $RPM_BUILD_ROOT string in nidas.pc, remove it for package
sed -r -i "s,$RPM_BUILD_ROOT,," \
        $RPM_BUILD_ROOT%{nidas_prefix}/%{_lib}/pkgconfig/nidas.pc

cp $RPM_BUILD_ROOT%{nidas_prefix}/%{_lib}/pkgconfig/nidas.pc \
        $RPM_BUILD_ROOT%{_libdir}/pkgconfig

install -m 0755 -d $RPM_BUILD_ROOT%{nidas_prefix}/scripts
install -m 0775 pkg_files%{nidas_prefix}/scripts/* $RPM_BUILD_ROOT%{nidas_prefix}/scripts

# install -m 0755 -d $RPM_BUILD_ROOT%{_sysconfdir}/init.d
# install -m 0775 pkg_files/root/etc/init.d/* $RPM_BUILD_ROOT%{_sysconfdir}/init.d

install -m 0755 -d $RPM_BUILD_ROOT%{_sysconfdir}/profile.d
install -m 0664 pkg_files/root/etc/profile.d/* $RPM_BUILD_ROOT%{_sysconfdir}/profile.d

install -m 0755 -d $RPM_BUILD_ROOT/usr/lib/udev/rules.d
install -m 0664 pkg_files/udev/rules.d/* $RPM_BUILD_ROOT/usr/lib/udev/rules.d

cp -r pkg_files/systemd ${RPM_BUILD_ROOT}%{nidas_prefix}

install -m 0755 -d $RPM_BUILD_ROOT%{_sysconfdir}/default
install -m 0664 pkg_files/root/etc/default/nidas-* $RPM_BUILD_ROOT%{_sysconfdir}/default

%post min

/sbin/ldconfig

%post libs

# If selinux is Enforcing, ldconfig can fail with permission denied if the
# policy and file contexts are not right on the libaries. Set the file context of
# library directory and contents to lib_t. I'm not sure at this point
# that this solves the whole issue, or whether a policy change is also required.
# There is some mystery in that ldconfig from root's interactive session never
# seems to fail with permission denied, but does fail from other contexts.
# During SCP, several times (probably after an rpm update) the nidas libs were
# not in the ld cache. I added ldconfig to rc.local and a crontab, and sometimes
# those failed with permission problems related to SELinux and /opt/nidas/{lib,lib64}.
# 
# The following is found in /etc/selinux/targeted/contexts/files/file_contexts
# /opt/(.*/)?lib(/.*)?	system_u:object_r:lib_t:s0
# in selinux-policy-targeted-3.14.2-57.fc29
# Looks like it doesn't match lib64 in /opt/nidas/lib64
# 
# To view:
# semanage fcontext --list -C | fgrep /opt/nidas
# /opt/(.*/)?var/lib(/.*)?  all files system_u:object_r:var_lib_t:s0

if /sbin/selinuxenabled; then
    /sbin/semanage fcontext -a -t lib_t %{nidas_prefix}/%{_lib}"(/.*)?" 2>/dev/null || :
    /sbin/restorecon -R %{nidas_prefix}/%{_lib} || :
fi
/sbin/ldconfig

%postun libs
if [ $1 -eq 0 ]; then # final removal
    /sbin/semanage fcontext -d -t lib_t %{nidas_prefix}/%{_lib}"(/.*)?" 2>/dev/null || :
fi

# If selinux is Enforcing, ldconfig can fail with permission denied if the
# policy and file contexts are not right. Set the file context of
# library directory and contents to lib_t. I'm not sure at this point
# that this solves the whole issue, or whether a policy change is also required.
# There is some mystery in that ldconfig from root's interactive session never
# seems to fail with permission denied, but does fail from other contexts.
# During SCP, several times (probably after an rpm update) the nidas libs were
# not in the ld cache. I added ldconfig to rc.local and a crontab, and sometimes
# those failed with permission problems related to SELinux and /opt/nidas/{lib,lib64}.
# 
# The following is found in /etc/selinux/targeted/contexts/files/file_contexts
# /opt/(.*/)?lib(/.*)?	system_u:object_r:lib_t:s0
# in selinux-policy-targeted-3.14.2-57.fc29
# Looks like it doesn't match lib64 in /opt/nidas/lib64
# 
# To view:
# semanage fcontext --list -C | fgrep /opt/nidas
# /opt/(.*/)?var/lib(/.*)?  all files system_u:object_r:var_lib_t:s0
#
# (gjg) I'm not sure about this approach, since the context needs to be
# installed even if selinux happens to be disabled at the moment.  The
# suggestion at the link below is to put the selinux contexts into a
# separate -selinux package, so they do not need to be installed on systems
# without selinux.  (I don't know if this is still current, but there are
# still examples of -selinux packages.)
#
# https://fedoraproject.org/wiki/PackagingDrafts/SELinux#File_contexts

if /sbin/selinuxenabled; then
    /sbin/semanage fcontext -a -t lib_t %{nidas_prefix}/%{_lib}"(/.*)?" 2>/dev/null || :
    /sbin/restorecon -R %{nidas_prefix}/%{_lib} || :
fi
/sbin/ldconfig

%pre daq
if [ "$1" -eq 1 ]; then
    echo "Edit %{_sysconfdir}/default/nidas-daq to set the DAQ_USER and DAQ_GROUP"
fi

%pre build
if [ $1 -eq 1 ]; then
    echo "Set BUILD_USER and BUILD_GROUP in %{_sysconfdir}/default/nidas-build.
Files installed in %{nidas_prefix} will then be owned by that user and group"
fi

%triggerin -n nidas-build -- nidas nidas-libs nidas-devel nidas-modules nidas-buildeol nidas-doxygen nidas-configedit nidas-autocal

[ -d %{nidas_prefix} ] || mkdir -p -m u=rwx,g=rwxs,o=rx %{nidas_prefix}

cf=%{_sysconfdir}/default/nidas-build

if [ -f $cf ]; then

    .  $cf 

    echo "nidas-build trigger: BUILD_USER=$BUILD_USER, BUILD_GROUP=$BUILD_GROUP read from $cf"

    if [ "$BUILD_USER" != root -o "$BUILD_GROUP" != root ]; then

        n=$(find %{nidas_prefix} \( \! -user $BUILD_USER -o \! -group $BUILD_GROUP \) -execdir chown -h $BUILD_USER:$BUILD_GROUP {} + -print | wc -l)

        find %{nidas_prefix} \! -type l \! -perm /g+w -execdir chmod g+w {} +

        [ $n -gt 0 ] && echo "nidas-build trigger: ownership of $n files under %{nidas_prefix} set to $BUILD_USER.$BUILD_GROUP, with group write"

        # chown on a file removes any associated capabilities
        if [ -x /usr/sbin/setcap ]; then
            arg="cap_sys_nice,cap_net_admin+p" 
            ckarg=$(echo $arg | cut -d, -f 1 | cut -d+ -f 1)

            for prog in %{nidas_prefix}/bin/{dsm_server,dsm,nidas_udp_relay}; do
                if [ -f $prog ] && ! getcap $prog | grep -F -q $ckarg; then
                    echo "nidas-build trigger: setcap $arg $prog"
                    setcap $arg $prog
                fi
            done
            arg="cap_sys_nice+p" 
            ckarg=$(echo $arg | cut -d, -f 1 | cut -d+ -f 1)
            for prog in %{nidas_prefix}/bin/{tee_tty,tee_i2c}; do
                if [ -f $prog ] && ! getcap $prog | grep -F -q $ckarg; then
                    echo "nidas-build trigger: setcap $arg $prog"
                    setcap $arg $prog
                fi
            done
        fi
    fi
fi

%post buildeol
cf=%{_sysconfdir}/default/nidas-build 
. $cf
if [ "$BUILD_GROUP" != eol ]; then
    sed -i -r -e 's/^ *BUILD_GROUP=.*/BUILD_GROUP=eol/g' $cf
fi

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(0775,root,root,2775)
%dir %{nidas_prefix}
%{nidas_prefix}/bin/arinc_out
%{nidas_prefix}/bin/ck_aout
%{nidas_prefix}/bin/ck_calfile
%{nidas_prefix}/bin/ck_xml
%{nidas_prefix}/bin/data_dump
%{nidas_prefix}/bin/data_stats
%{nidas_prefix}/bin/datasets
%{nidas_prefix}/bin/dmd_mmat_test
%{nidas_prefix}/bin/dmd_mmat_vin_limit_test
%{nidas_prefix}/bin/dmd_mmat_vout_const
%{nidas_prefix}/bin/dsc_a2d_ck
%caps(cap_sys_nice,cap_net_admin+p) %{nidas_prefix}/bin/dsm_server
%caps(cap_sys_nice,cap_net_admin+p) %{nidas_prefix}/bin/dsm
%caps(cap_sys_nice,cap_net_admin+p) %{nidas_prefix}/bin/nidas_udp_relay
%caps(cap_sys_nice+p) %{nidas_prefix}/bin/tee_tty
%{nidas_prefix}/bin/extract2d
%{nidas_prefix}/bin/extractDMT
%{nidas_prefix}/bin/ir104
%{nidas_prefix}/bin/lidar_vel
%{nidas_prefix}/bin/merge_verify
%{nidas_prefix}/bin/n_hdr_util
%{nidas_prefix}/bin/nidsmerge
%{nidas_prefix}/bin/arl-ingest
%{nidas_prefix}/bin/proj_configs
%{nidas_prefix}/bin/prep
%{nidas_prefix}/bin/rserial
%{nidas_prefix}/bin/sensor_extract
%{nidas_prefix}/bin/sensor_sim
%{nidas_prefix}/bin/sing
%{nidas_prefix}/bin/statsproc
%{nidas_prefix}/bin/status_listener
%{nidas_prefix}/bin/sync_dump
%{nidas_prefix}/bin/sync_server
%{nidas_prefix}/bin/utime
%{nidas_prefix}/bin/xml_dump
%{nidas_prefix}/scripts/*
%{nidas_prefix}/bin/data_influxdb

%config(noreplace) %{_sysconfdir}/profile.d/nidas.sh
%config(noreplace) %{_sysconfdir}/profile.d/nidas.csh

%attr(0664,-,-) %{nidas_prefix}/share/xml/nidas.xsd

%{nidas_prefix}/systemd

%files libs
%defattr(0775,root,root,2775)
%{nidas_prefix}/%{_lib}/libnidas_util.so.*
%{nidas_prefix}/%{_lib}/libnidas.so.*
%{nidas_prefix}/%{_lib}/libnidas_dynld.so.*
# %%{nidas_prefix}/%%{_lib}/nidas_dynld_iss_TiltSensor.so.*
# %%{nidas_prefix}/%%{_lib}/nidas_dynld_iss_WICORSensor.so.*

%if %{with modules}
%files modules
%defattr(0775,root,root,2775)
%if %{with arinc}
%{nidas_prefix}/modules/arinc.ko
%endif
%{nidas_prefix}/modules/dmd_mmat.ko
%{nidas_prefix}/modules/emerald.ko
%{nidas_prefix}/modules/gpio_mm.ko
%{nidas_prefix}/modules/ir104.ko
%{nidas_prefix}/modules/lamsx.ko
%{nidas_prefix}/modules/mesa.ko
%{nidas_prefix}/modules/ncar_a2d.ko
%{nidas_prefix}/modules/nidas_util.ko
%{nidas_prefix}/modules/pc104sg.ko
%{nidas_prefix}/modules/pcmcom8.ko
%{nidas_prefix}/modules/short_filters.ko
%{nidas_prefix}/modules/usbtwod.ko
%{nidas_prefix}/firmware
%endif

%if %{with autocal}
%files autocal
%defattr(0775,root,root,2775)
%{nidas_prefix}/bin/auto_cal
%endif

%if %{with configedit}
%files configedit
%defattr(0775,root,root,2775)
%{nidas_prefix}/bin/configedit
%endif

%files min
%defattr(-,root,root,-)
%{_sysconfdir}/ld.so.conf.d/nidas.conf

%files daq
%defattr(0775,root,root,0775)
%config /usr/lib/udev/rules.d/99-nidas.rules
%config(noreplace) %{_sysconfdir}/default/nidas-daq
# %%config(noreplace) %%{_sysconfdir}/init.d/dsm_server
# %%config(noreplace) %%{_sysconfdir}/init.d/dsm

%files devel
%defattr(0664,root,root,2775)
%{nidas_prefix}/include/nidas/Config.h
%{nidas_prefix}/include/nidas/Revision.h
%{nidas_prefix}/include/nidas/util
%{nidas_prefix}/include/nidas/core
%{nidas_prefix}/include/nidas/dynld
%{nidas_prefix}/include/nidas/linux
%{nidas_prefix}/%{_lib}/libnidas_util.so
%{nidas_prefix}/%{_lib}/libnidas_util.a
%{nidas_prefix}/%{_lib}/libnidas.so
%{nidas_prefix}/%{_lib}/libnidas_dynld.so
# %%{nidas_prefix}/%%{_lib}/nidas_dynld_iss_TiltSensor.so
# %%{nidas_prefix}/%%{_lib}/nidas_dynld_iss_WICORSensor.so
%config %{nidas_prefix}/%{_lib}/pkgconfig/nidas.pc
%config %{_libdir}/pkgconfig/nidas.pc

%files build
%defattr(-,root,root,-)
%config(noreplace) %attr(0664,-,-) %{_sysconfdir}/default/nidas-build

%files buildeol

%changelog
