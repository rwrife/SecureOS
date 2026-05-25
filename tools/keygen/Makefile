# Makefile for keygen — host-side key generation tool for SecureOS code signing
#
# Compiles keygen from main.c and the kernel crypto sources.
# This tool runs on the build host, not on the target OS.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2

CRYPTO_DIR   = ../../kernel/crypto
SRC          = main.c $(CRYPTO_DIR)/sha512.c $(CRYPTO_DIR)/ed25519.c $(CRYPTO_DIR)/cert.c
OUT          = keygen

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRC) $(CRYPTO_DIR)/sha512.h $(CRYPTO_DIR)/ed25519.h $(CRYPTO_DIR)/cert.h $(CRYPTO_DIR)/root_key.h
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

clean:
	rm -f $(OUT)
