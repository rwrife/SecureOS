/**
 * @file main.c
 * @brief Host-side key generation tool for SecureOS code signing.
 *
 * Purpose:
 *   Generates or derives the root and intermediate Ed25519 keypairs used for
 *   SecureOS binary code signing.  Produces:
 *     - artifacts/keys/root.pub        (32 bytes — root public key)
 *     - artifacts/keys/intermediate.seed (32 bytes — intermediate private seed)
 *     - artifacts/keys/intermediate.cert (132 bytes — SCRT certificate)
 *
 *   The certificate binds the intermediate public key to the root key via an
 *   Ed25519 signature, matching the format expected by sof_verify_signature().
 *
 * Usage:
 *   keygen [--out-dir <dir>]
 *     Derives keys from the deterministic seeds baked into root_key.h and
 *     writes the signing artifacts to <dir> (default: artifacts/keys).
 *
 * Interactions:
 *   - kernel/crypto/ed25519.{h,c} — keypair derivation and signing
 *   - kernel/crypto/cert.{h,c} — certificate building and serialization
 *   - kernel/crypto/root_key.h — deterministic seeds for reproducible builds
 *   - build/scripts/generate_keys.sh — invokes this tool during the build
 *
 * Launched by:
 *   Called by build/scripts/generate_keys.sh as part of the build pipeline.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/crypto/ed25519.h"
#include "../../kernel/crypto/cert.h"
#include "../../kernel/crypto/root_key.h"

static int write_binary_file(const char *path, const uint8_t *data, size_t len) {
  FILE *fp = fopen(path, "wb");
  if (fp == NULL) {
    fprintf(stderr, "keygen: cannot open '%s' for writing\n", path);
    return 0;
  }
  if (fwrite(data, 1, len, fp) != len) {
    fprintf(stderr, "keygen: write error to '%s'\n", path);
    fclose(fp);
    return 0;
  }
  fclose(fp);
  return 1;
}

int main(int argc, char **argv) {
  const char *out_dir = "artifacts/keys";
  uint8_t root_seed[32], int_seed[32];
  uint8_t root_pub[32], root_priv[64];
  uint8_t int_pub[32], int_priv[64];
  secureos_cert_t cert;
  uint8_t cert_buf[SECUREOS_CERT_TOTAL_SIZE];
  char path_buf[256];
  size_t i;

  /* Parse arguments */
  for (i = 1; i < (size_t)argc; ++i) {
    if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < (size_t)argc) {
      out_dir = argv[++i];
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: keygen [--out-dir <dir>]\n");
      printf("  Generates signing keys and certificate for SecureOS code signing.\n");
      printf("  Default output: artifacts/keys/\n");
      return 0;
    }
  }

  /* Copy seeds from the baked-in constants (deterministic, reproducible) */
  for (i = 0; i < 32; ++i) { root_seed[i] = SECUREOS_ROOT_SEED[i]; }
  for (i = 0; i < 32; ++i) { int_seed[i] = SECUREOS_INTERMEDIATE_SEED[i]; }

  /* Derive keypairs */
  ed25519_create_keypair(root_seed, root_pub, root_priv);
  ed25519_create_keypair(int_seed, int_pub, int_priv);

  /* Build certificate: root signs intermediate's public key */
  cert_build(root_pub, root_priv, int_pub, &cert);
  cert_serialize(&cert, cert_buf);

  /* Write root.pub */
  snprintf(path_buf, sizeof(path_buf), "%s/root.pub", out_dir);
  if (!write_binary_file(path_buf, root_pub, 32)) { return 1; }
  printf("keygen: wrote %s (32 bytes)\n", path_buf);

  /* Write intermediate.seed */
  snprintf(path_buf, sizeof(path_buf), "%s/intermediate.seed", out_dir);
  if (!write_binary_file(path_buf, int_seed, 32)) { return 1; }
  printf("keygen: wrote %s (32 bytes)\n", path_buf);

  /* Write intermediate.cert */
  snprintf(path_buf, sizeof(path_buf), "%s/intermediate.cert", out_dir);
  if (!write_binary_file(path_buf, cert_buf, SECUREOS_CERT_TOTAL_SIZE)) { return 1; }
  printf("keygen: wrote %s (%d bytes)\n", path_buf, SECUREOS_CERT_TOTAL_SIZE);

  printf("keygen: done — all signing materials generated in %s/\n", out_dir);
  return 0;
}
