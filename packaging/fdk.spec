Name:           fdk
Version:        0.1.0
Release:        1%{?dist}
Summary:        Faded Dream Kit — lightweight GUI toolkit for Linux
License:        Proprietary
URL:            https://github.com/FemBoyGamerTechGuy/fdk

Source0:        fdk-v%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  freetype-devel
BuildRequires:  wayland-devel
BuildRequires:  wayland-protocols-devel
BuildRequires:  libX11-devel
BuildRequires:  libXcursor-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  mesa-libEGL-devel

Requires:       freetype
Requires:       wayland
Requires:       libX11
Requires:       libXcursor

%description
FDK is a from-scratch GUI toolkit for Linux with Wayland and X11 backends,
software and OpenGL rendering, and a built-in theming system.
No D-Bus, no GTK, no systemd dependencies.

%package        devel
Summary:        Development files for FDK
Requires:       %{name} = %{version}-%{release}

%description    devel
Headers, static library, pkg-config file, and CMake config for building
applications with FDK.

%package        tools
Summary:        FDK theme management CLI (fdk-theme)
Requires:       %{name} = %{version}-%{release}

%description    tools
Command-line tool for setting, listing, and inspecting FDK themes.
Manages ~/.FDKthemes/ and ~/.config/FDK/fdk.conf.

%prep
%setup -q -n fdk-v%{version}

%build
%cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DFDK_BUILD_EXAMPLES=OFF \
    -DFDK_BUILD_TESTS=OFF \
    -DFDK_BUILD_TOOLS=ON \
    -DFDK_BUILD_SHARED=ON \
    -DFDK_BUILD_STATIC=ON \
    -GNinja
%cmake_build

%install
%cmake_install

%files
%license LICENSE sublicense/SUBLICENSE
%{_libdir}/libfdk.so.*

%files devel
%{_includedir}/fdk/
%{_libdir}/libfdk.a
%{_libdir}/libfdk.so
%{_libdir}/pkgconfig/fdk.pc
%{_libdir}/cmake/FDK/

%files tools
%{_bindir}/fdk-theme
