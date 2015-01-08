#!/bin/bash

if [ -e ".done" ]; then
	echo "Already done."
	exit 1
fi

if [ -e "/usr/bin/clang" ]; then
	echo "/usr/bin/clang" > .compiler
elif [ -e "/usr/bin/gcc" ]; then
	echo "/usr/bin/gcc" > .compiler
elif [ -e "/usr/bin/cc" ]; then
	echo "/usr/bin/cc" > .compiler
else
	echo "No suitable compiler found."
	exit 1
fi

if [ `uname` = "Darwin" ] || [ `uname` = "FreeBSD" ]; then
	mv src/Makefile src/Makefile.old
	mv src/Makefile.bsd src/Makefile
elif [ `uname` = "Linux" ]; then
	mv src/Makefile src/Makefile.old
	mv src/Makefile.linux src/Makefile
fi

if [ $# -eq 1 ]; then
	if [ "${1}" == "fsma" ]; then
		echo "-DUSE_LIBFSMA" > .ecflags
		echo "-lfsma" > .elflags
	fi
else
	touch .ecflags
	touch .elflags
fi

if [ `uname -m` = "armv6l" ]; then
	if [ "`cat .target`" == "rpi" ]; then
		echo "-ccc-host-triple armv6-unknown-eabi -march=armv6 -mfpu=vfp -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -mfloat-abi=hard" > .archflags
	else
		echo "-march=armv6" > .archflags
	fi
else
	echo "" > .archflags
fi

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

