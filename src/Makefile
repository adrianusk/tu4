#
# Makefile for Linux
#

-include ../make.config

PREFIX ?= /usr/local
UI ?= sdl2
CONF ?= xml

bindir=$(PREFIX)/bin
libdir=$(PREFIX)/lib
datadir=$(PREFIX)/share

# Supported UI backends: sdl2 (default), sdl, sdl3. (The Allegro backend and
# the OpenGL/GPU renderer were removed — tu4 is a text-mode SDL build.)
ifeq ($(UI), sdl)
UILIBS=$(shell sdl-config --libs) -lSDL_mixer
UIFLAGS=$(shell sdl-config --cflags)
else ifeq ($(UI), sdl2)
UILIBS=$(shell sdl2-config --libs) -lSDL2_mixer
UIFLAGS=$(shell sdl2-config --cflags)
else ifeq ($(UI), sdl3)
UILIBS=$(shell pkg-config --libs sdl3 SDL3_mixer)
UIFLAGS=$(shell pkg-config --cflags sdl3 SDL3_mixer)
else
$(error Unsupported UI "$(UI)": use sdl2 (default), sdl, or sdl3)
endif

ifeq ($(CONF),xml)
	UIFLAGS+=$(shell xml2-config --cflags)
	UILIBS+=$(shell xml2-config --libs)
else
	UIFLAGS+=-DUSE_BORON -DCONF_MODULE
	UILIBS+=-lboron
endif

FEATURES=-DHAVE_BACKTRACE=1 -DHAVE_VARIADIC_MACROS=1
DEBUGCXXFLAGS=-rdynamic -g
CXXFLAGS=$(FEATURES) -Wall -I. -Isupport $(UIFLAGS) -DVERSION=\"$(VERSION)\" $(DEBUGCXXFLAGS)
CFLAGS=$(CXXFLAGS)
LIBS=$(UILIBS) -lz
INSTALL=install

ifeq ($(STATIC_GCC_LIBS),true)
    LDFLAGS+=-L. -static-libgcc
endif

include Makefile.common

# Resources are installed under $(datadir)/tu4, which is one of the
# runtime root resource paths searched by u4file.cpp. The tu4 binary
# then finds conf/, graphics/, mid/ and sound/ as subdirectories there.
#
# Note: tu4 is a text-mode build (CONF=xml, GPU=none). It reads loose
# conf/*.xml plus the per-theme fragments in conf/themes/ (XIncluded by
# graphics-text.xml) and the character-based graphics themes (U5-EGA,
# EGA) at runtime -- there is no Ultima-IV.mod bundle and no VGA/hires PNG
# themes to install. PC9801 is a future add-on module, not installed here.
# The Ultima IV for DOS game data (ultima4/) is user-supplied and is not
# installed here.
resdir=$(datadir)/tu4

install::
	$(INSTALL) -D $(MAIN) $(bindir)/$(MAIN)
	$(INSTALL) -D ../graphics/tu4.png $(datadir)/icons/hicolor/48x48/apps/tu4.png
	$(INSTALL) -D ../packaging/tu4.desktop $(datadir)/applications/tu4.desktop
	mkdir -p $(resdir)/mid
	$(INSTALL) ../mid/*.mid $(resdir)/mid
	$(INSTALL) ../mid/*.it $(resdir)/mid
	mkdir -p $(resdir)/sound
	$(INSTALL) ../sound/*.ogg $(resdir)/sound
	mkdir -p $(resdir)/conf
	$(INSTALL) ../conf/*.xml $(resdir)/conf
	$(INSTALL) ../conf/sigdata.txt $(resdir)/conf
	mkdir -p $(resdir)/conf/dtd
	$(INSTALL) ../conf/dtd/*.dtd $(resdir)/conf/dtd
	mkdir -p $(resdir)/conf/themes
	$(INSTALL) ../conf/themes/U5-EGA.xml $(resdir)/conf/themes
	$(INSTALL) ../conf/themes/EGA.xml $(resdir)/conf/themes
	mkdir -p $(resdir)/graphics/U5-EGA
	mkdir -p $(resdir)/graphics/EGA
	$(INSTALL) ../graphics/U5-EGA/*.ASP $(resdir)/graphics/U5-EGA
	$(INSTALL) ../graphics/U5-EGA/cp437_8x8.bin $(resdir)/graphics/U5-EGA
	$(INSTALL) ../graphics/U5-EGA/palette.bin $(resdir)/graphics/U5-EGA
	$(INSTALL) ../graphics/EGA/*.ASP $(resdir)/graphics/EGA
	$(INSTALL) ../graphics/EGA/cp437_8x8.bin $(resdir)/graphics/EGA
	$(INSTALL) ../graphics/EGA/palette.bin $(resdir)/graphics/EGA
	$(INSTALL) ../graphics/tu4.bmp $(resdir)/graphics/tu4.bmp


# use GCC's automatic dependency generation
.depends::
	rm -f .depends
	$(CC) $(CFLAGS) -MM $(CSRCS) >> .depends
	$(CXX) $(CXXFLAGS) -MM $(CXXSRCS) >> .depends

# a target to build without dependencies on libstdc++, libgcc_s
all.static_gcc_libs:
	ln -s `$(CXX) -print-file-name=libstdc++.a` .
	$(MAKE) STATIC_GCC_LIBS=true
	rm -f libstdc++.a

-include .depends

