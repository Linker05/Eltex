obj-m += netlink.o

KBUILD ?= /lib/modules/$(shell uname -r)/build

CFLAGS ?= -O2 -g

MODULE_SRC ?= $(PWD)
OUTPUT ?= $(PWD)/bin

all: create_output user kernel

user:
	$(CC) $(CFLAGS) user.c -o $(OUTPUT)/user

kernel:
	make -C $(KBUILD) M=$(MODULE_SRC) MO=$(OUTPUT) modules

create_output:
	mkdir -p $(OUTPUT)

clean:
	make -C $(KBUILD) M=$(MODULE_SRC) MO=$(OUTPUT) clean
	rm -rf $(OUTPUT)/*
