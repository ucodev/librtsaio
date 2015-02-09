#!/bin/sh

## Detect compiler ##
. ./lib/sh/compiler.inc

## Detect architecture ##
. ./lib/sh/arch.inc

## Extra target checks ##
if [ `uname` = "Darwin" ]; then
	mv src/Makefile src/Makefile.old
	cp src/Makefile.osx src/Makefile
elif [ `uname` = "FreeBSD" ]; then
	mv src/Makefile src/Makefile.old
	cp src/Makefile.bsd src/Makefile
elif [ `uname` = "FreeBSD" ]; then
	mv src/Makefile src/Makefile.old
	cp src/Makefile.bsd src/Makefile
elif [ `uname` = "Linux" ]; then
	mv src/Makefile src/Makefile.old
	cp src/Makefile.linux src/Makefile
fi

## Options ##
if [ $# -eq 1 ]; then
	if [ "${1}" == "fsma" ]; then
		echo "-DUSE_LIBFSMA" > .ecflags
		echo "-lfsma" > .elflags
	fi
else
	touch .ecflags
	touch .elflags
fi

# Build
make

if [ $? -ne 0 ]; then
	echo "Build failed."

	if [ `uname` = "Darwin" ] || [ `uname` = "FreeBSD" ]; then
		mv src/Makefile src/Makefile.bsd
		mv src/Makefile.old src/Makefile
	elif [ `uname` = "Linux" ]; then
		mv src/Makefile src/Makefile.linux
		mv src/Makefile.old src/Makefile
	fi

	exit 1
fi

touch .done

echo "Build completed."

exit 0

