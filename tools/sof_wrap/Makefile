# Makefile for sof_wrap — host-side SOF wrapping tool
#
# Compiles sof_wrap from main.c and kernel/format/sof.c.
# This tool runs on the build host (inside the Docker toolchain),
# not on the target OS.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2

KERNEL_DIR   = ../../kernel/format
CRYPTO_DIR   = ../../kernel/crypto
SRC          = main.c $(KERNEL_DIR)/sof.c $(CRYPTO_DIR)/sha512.c $(CRYPTO_DIR)/ed25519.c $(CRYPTO_DIR)/cert.c
OUT          = sof_wrap

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRC) $(KERNEL_DIR)/sof.h $(CRYPTO_DIR)/sha512.h $(CRYPTO_DIR)/ed25519.h $(CRYPTO_DIR)/cert.h
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

clean:
	rm -f $(OUT)