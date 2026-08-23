obj-m += proc.o

KBUILD ?= /lib/modules/$(shell uname -r)/build

MODULE_SRC ?= $(PWD)
OUTPUT ?= $(PWD)/bin

all: create_output
	make -C $(KBUILD) M=$(MODULE_SRC) MO=$(OUTPUT) modules

create_output:
	mkdir -p $(OUTPUT)

clean:
	make -C $(KBUILD) M=$(MODULE_SRC) MO=$(OUTPUT) clean
	rm -rf $(OUTPUT)/*
