%define release_tag 0
%define release_stamp 20221024.af91da17

%def_disable debug
%def_enable tests
# required network
%def_disable gst_player_tests
%def_disable check

%define api_ver 1.0
%define ver_major 1.18

%define _gst_libdir %_libdir/gstreamer-%api_ver
%define _gtk_docdir %_datadir/gtk-doc/html

%def_disable gtk_doc

Name: gst-omx
Version: %release_tag
Release: %release_stamp.elv.alt1

Summary: GStreamer plug-in to use OpenMAX API.
Group: System/Libraries
License: LGPLv2+

Source: %name-%release_stamp.tar.gz

BuildRequires(pre): rpm-macros-meson rpm-build-gir
BuildRequires: meson
BuildRequires: gst-plugins%api_ver-devel >= %ver_major
BuildRequires: mcom03-vpu-libs-devel

Requires: gstreamer%api_ver >= %ver_major
Requires: gst-plugins-base%api_ver >= %ver_major
Requires: mcom03-vpu-libs

%description
GStreamer plug-in to use OpenMAX API.

%prep
%setup -n %name

%build
%meson -Db_pie=false -Dstrip=false -Dexamples=disabled -Dtests=disabled \
    -Dtools=disabled -Ddoc=disabled -Dtarget=mcom03 -Dheader_path=%_includedir/OMXIL/1.1.2
%meson_build

%install
%meson_install
%__sed -e 's!/usr/lib/libmveomx!%_libdir/libmveomx!g' -i %buildroot%_sysconfdir/xdg/gstomx.conf

%files
%dir %_gst_libdir
%_gst_libdir/*.so
%_sysconfdir/xdg/gstomx.conf
