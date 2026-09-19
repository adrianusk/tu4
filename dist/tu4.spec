Name:      tu4
Version:   1.0
Release:   1
Epoch:     0
Summary:   tu4 - Ultima IV Text Mode

Group:     Amusements/Games
License:   GPL
URL:       http://xu4.sourceforge.net/
Source0:   tu4-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Prefix:    /usr

BuildRequires: SDL2-devel SDL2_mixer-devel libxml2-devel zlib-devel

%description
TU4 is a text-mode (80x50 CP437, 16-color EGA) recreation of the
classic computer game Ultima IV, adapted from the xu4 project. It
renders the entire game with characters and color attributes instead
of pixels, while keeping xu4's game logic, coordinate systems, map
handling and save files.

TU4 isn't a new game based on the Ultima IV story -- it is a faithful
recreation of the old game, rendered in a DOS-console style. The
Ultima IV for DOS game data is required at runtime and is not included.

%prep
%setup -n tu4

%build
cd src && make bindir=%{_bindir} datadir=%{_datadir} libdir=%{_libdir} UI=sdl2 CONF=xml GPU=none all.static_gcc_libs

%install
cd src && %{makeinstall}

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%doc README.md AUTHORS COPYING
%{_bindir}/tu4
%{_datadir}/icons/hicolor/48x48/apps/tu4.png
%{_datadir}/applications/tu4.desktop
%{_datadir}/tu4/conf/*.xml
%{_datadir}/tu4/conf/sigdata.txt
%{_datadir}/tu4/conf/dtd/*.dtd
%{_datadir}/tu4/graphics/U5-EGA/*
%{_datadir}/tu4/graphics/PC9801/*
%{_datadir}/tu4/graphics/tu4.bmp
%{_datadir}/tu4/mid/*
%{_datadir}/tu4/sound/*.ogg

%changelog
* Fri Sep 04 2026 ASCII Dragon
- Adapted from xu4.spec for the tu4 text-mode port: renamed package
  to tu4, switched build to UI=sdl2 CONF=xml GPU=none, dropped the
  Ultima IV zip downloads (game data is user-supplied) and the DOS
  decode/encode utilities, and updated the file list to the tu4
  resource layout (conf/, text-mode graphics themes U5-EGA & PC9801,
  mid/, sound/ under %{_datadir}/tu4).
