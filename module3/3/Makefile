CC=gcc
CFLAGS=-O2 -g

DEBUG=on

SOURCE_DIR=.
SOURCES=$(wildcard ${SOURCE_DIR}/*.c)
BIN_DIR=./bin
OUTPUT=${BIN_DIR}/program

ifeq (${DEBUG},on)
	CFLAGS += -D DEBUG
endif

build: clean compile chmod

clean:
	rm -rf ${BIN_DIR}/*

compile:
	mkdir -p ${BIN_DIR}
	${CC} ${CFLAGS} ${SOURCES} -o ${OUTPUT}

chmod:
	chmod u+x ${OUTPUT}