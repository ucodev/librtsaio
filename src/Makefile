CC=`cat ../.compiler`
DEFS=
CCFLAGS=${DEFS} -fPIC -g -pthread -O2 -fstrict-aliasing -Wall -Werror -pedantic
ECFLAGS=`cat ../.ecflags`
ARCHFLAGS=`cat ../.archflags`
LDFLAGS=-shared -lpall -lpthread
ELFLAGS=`cat ../.elflags`
TARGET=librtsaio.so

all:
	${CC} ${ECFLAGS} ${CCFLAGS} ${ARCHFLAGS} -c asyncop.c
	${CC} ${ECFLAGS} ${CCFLAGS} ${ARCHFLAGS} -c mm.c
	${CC} ${ECFLAGS} ${CCFLAGS} ${ARCHFLAGS} -c rtsaio.c
	${CC} ${ECFLAGS} ${CCFLAGS} ${ARCHFLAGS} -c timespec.c
	${CC} ${LDFLAGS} -o ${TARGET} asyncop.o mm.o rtsaio.o timespec.o ${ELFLAGS}

clean:
	rm -f *.o
	rm -f ${TARGET}
