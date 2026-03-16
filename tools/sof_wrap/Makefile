# Makefile for sof_wrap — host-side SOF wrapping tool
#
# Compiles sof_wrap from main.c and kernel/format/sof.c.
# This tool runs on the build host (inside the Docker toolchain),
# not on the target OS.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2

KERNEL_DIR = ../../kernel/format
SRC        = main.c $(KERNEL_DIR)/sof.c
OUT        = sof_wrap

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRC) $(KERNEL_DIR)/sof.h
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

clean:
	rm -f $(OUT)