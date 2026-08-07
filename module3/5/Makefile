CC=gcc
CFLAGS=-O0 -g

DEBUG=on

SOURCE_DIR=.
SOURCES=$(wildcard ${SOURCE_DIR}/*.c)
BIN_DIR=./bin

ifeq (${DEBUG},on)
	CFLAGS += -D DEBUG
endif

build: clean compile chmod

clean:
	rm -rf ${BIN_DIR}/*

compile:
	mkdir -p ${BIN_DIR}
	${CC} ${CFLAGS} producer.c -o ${BIN_DIR}/producer
	${CC} ${CFLAGS} consumer.c -o ${BIN_DIR}/consumer

chmod:
	chmod u+x ${BIN_DIR}/*