CC=`cat ../.compiler`
INCLUDEDIRS=-I../include
CCFLAGS=
ECFLAGS=`cat ../.ecflags`
ARCHFLAGS=`cat ../.archflags`
LDFLAGS=-shared -lpall
ELFLAGS=`cat ../.elflags`
TARGET=librtsaio.`cat ../.extlib`

all:
	${CC} ${INCLUDEDIRS} ${ECFLAGS} ${CCFLAGS} ${ARCHFLAGS} -c asyncop.c
	${CC} ${INCLUDEDIRS} ${ECFLAGS} ${CCFLAGS} ${ARCHFLAGS} -c mm.c
	${CC} ${INCLUDEDIRS} ${ECFLAGS} ${CCFLAGS} ${ARCHFLAGS} -c rtsaio.c
	${CC} ${INCLUDEDIRS} ${ECFLAGS} ${CCFLAGS} ${ARCHFLAGS} -c timespec.c
	${CC} ${LDFLAGS} -o ${TARGET} asyncop.o rtsaio.o timespec.o ${ELFLAGS}

clean:
	rm -f *.o
	rm -f ${TARGET}
