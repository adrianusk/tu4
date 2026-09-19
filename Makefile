# GNU Makefile for Linux & macOS.

OS := $(shell uname)
ifeq ($(OS), Darwin)
MFILE_OS=Makefile.macosx
else
MFILE_OS=Makefile
endif

all:
	$(MAKE) -C src -f $(MFILE_OS)
	cp src/tu4 .

.PHONY: all clean download snapshot

clean:
	make -C src -f $(MFILE_OS) clean
	rm -f Ultima-IV.mod

download: ultima4.zip u4upgrad.zip

ultima4.zip:
	curl -sSL -o $@ http://ultima.thatfleminggent.com/ultima4.zip

u4upgrad.zip:
	curl -sSL -o $@ http://sourceforge.net/projects/xu4/files/Ultima%204%20VGA%20Upgrade/1.3/u4upgrad.zip

snapshot: Ultima-IV.mod
	@rm -f project.tar.gz
	copr -c
	tools/cbuild windows
	tools/cbuild linux
	scp /tmp/xu4-linux.tar.gz /tmp/xu4-win32.zip $(SF_USER),xu4@web.sourceforge.net:/home/project-web/xu4/web/download
