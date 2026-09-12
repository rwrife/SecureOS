/**
 * @file main.c
 * @brief Binary-safe filesystem I/O evidence application (issue #765).
 *
 * Purpose:
 *   Runs in the ordinary booted guest image and exercises the
 *   length-bearing native file I/O operations (`os_fs_read_file_bytes`
 *   / `os_fs_write_file_bytes`, bridge v5) end-to-end through the real
 *   kernel FS path (fs_read_file_bytes / fs_write_file_bytes on FAT),
 *   behind the unchanged CAP_FS_READ/CAP_FS_WRITE + CAP_DISK_IO_REQUEST
 *   + per-operation disk-IO consent gates.
 *
 *   Emits deterministic `BINFS:PASS:<name>` markers only when the
 *   byte-exact contract holds on the guest path:
 *     - nul_roundtrip:  create+overwrite a file whose payload has
 *       leading, interior, and trailing NUL bytes; read back reports
 *       the exact length and byte content (no strnlen-style truncation).
 *     - append_bytes:   create + append binary payloads containing NUL
 *       bytes; the combined length and bytes survive the round trip.
 *     - capacity_error: reading into an undersized buffer fails with
 *       an explicit error and reports out_len == 0 (short-read data is
 *       never silently delivered).
 *     - notfound:       a missing file reports NOT_FOUND (distinct from
 *       an empty-file EOF) with out_len pinned to 0.
 *     - text_len_compat: the legacy text write path followed by the
 *       bytes read path reports the exact stored byte count, proving
 *       existing text callers keep their behavior.
 *
 * Interactions:
 *   - secureos_api.h: the only OS surface used. Console output goes
 *     through os_console_write; every file operation goes through the
 *     binary-safe wrappers and inherits the launcher's capability and
 *     consent gates (each fresh operation+path pair triggers the
 *     console's disk-IO authorization prompt).
 *
 * Launched by:
 *   The `kernel_binfs` QEMU gate (build/scripts/run_qemu.sh --test
 *   kernel_binfs), which boots the ordinary ISO + seeded disk image,
 *   answers the consent prompts, and asserts the BINFS markers plus a
 *   clean `exit pass`. Built by build/scripts/build_user_app.sh and
 *   staged at /apps/binfs.bin by build/scripts/build_disk_image.sh.
 */

#include "secureos_api.h"

static void app_log(const char *message) {
  (void)os_console_write(message);
}

static int bytes_equal(const void *a, const void *b, unsigned int n) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  unsigned int i;
  for (i = 0u; i < n; ++i) {
    if (pa[i] != pb[i]) {
      return 0;
    }
  }
  return 1;
}

int main(void) {
  /* Leading, interior, and trailing NULs plus high-bit bytes — the
   * exact payload classes the old strnlen-based fd path corrupted. */
  static const unsigned char payload_nul[12] = {
      0x00u, 0x01u, 0x02u, 0x00u, 0x04u, 0xFFu,
      0x80u, 0x00u, 0x7Fu, 0x41u, 0x00u, 0x00u};
  static const unsigned char payload_a[4] = {0x00u, 0xFEu, 0x00u, 0x01u};
  static const unsigned char payload_b[3] = {0x00u, 0x00u, 0xFDu};
  unsigned char read_buf[32];
  unsigned char tiny[4];
  const char *text_payload = "hello-text";
  unsigned int out_len = 999u;
  os_status_t status;
  int failures = 0;

  app_log("BINFS:start\n");

  /* 1) nul_roundtrip: create/overwrite with an NUL-heavy payload. */
  status = os_fs_write_file_bytes("binnul.dat", payload_nul,
                                  sizeof(payload_nul), 0);
  if (status == OS_STATUS_OK) {
    out_len = 999u;
    status = os_fs_read_file_bytes("binnul.dat", read_buf, sizeof(read_buf),
                                   &out_len);
    if (status == OS_STATUS_OK && out_len == sizeof(payload_nul) &&
        bytes_equal(read_buf, payload_nul, sizeof(payload_nul))) {
      app_log("BINFS:PASS:nul_roundtrip\n");
    } else {
      app_log("BINFS:FAIL:nul_roundtrip\n");
      failures++;
    }
  } else {
    app_log("BINFS:FAIL:nul_roundtrip:write\n");
    failures++;
  }

  /* 2) append_bytes: create then append, both carrying NUL bytes. */
  status = os_fs_write_file_bytes("binapp.dat", payload_a,
                                  sizeof(payload_a), 0);
  if (status == OS_STATUS_OK) {
    status = os_fs_write_file_bytes("binapp.dat", payload_b,
                                    sizeof(payload_b), 1);
  }
  if (status == OS_STATUS_OK) {
    out_len = 999u;
    status = os_fs_read_file_bytes("binapp.dat", read_buf, sizeof(read_buf),
                                   &out_len);
    if (status == OS_STATUS_OK && out_len == sizeof(payload_a) + sizeof(payload_b) &&
        bytes_equal(read_buf, payload_a, sizeof(payload_a)) &&
        bytes_equal(read_buf + sizeof(payload_a), payload_b, sizeof(payload_b))) {
      app_log("BINFS:PASS:append_bytes\n");
    } else {
      app_log("BINFS:FAIL:append_bytes\n");
      failures++;
    }
  } else {
    app_log("BINFS:FAIL:append_bytes:write\n");
    failures++;
  }

  /* 3) capacity_error: undersized read fails cleanly with out_len 0. */
  out_len = 999u;
  status = os_fs_read_file_bytes("binnul.dat", tiny, sizeof(tiny), &out_len);
  if (status != OS_STATUS_OK && out_len == 0u) {
    app_log("BINFS:PASS:capacity_error\n");
  } else {
    app_log("BINFS:FAIL:capacity_error\n");
    failures++;
  }

  /* 4) notfound: missing file is NOT_FOUND with out_len 0, distinct
   *    from the empty-file OK-with-zero-length EOF contract. */
  out_len = 999u;
  status = os_fs_read_file_bytes("binmiss.dat", read_buf,
                                 sizeof(read_buf), &out_len);
  if (status == OS_STATUS_NOT_FOUND && out_len == 0u) {
    app_log("BINFS:PASS:notfound\n");
  } else {
    app_log("BINFS:FAIL:notfound\n");
    failures++;
  }

  /* 5) text_len_compat: legacy text write stores exactly the string
   *    bytes; the bytes-read path reports that exact length. */
  status = os_fs_write_file("binfs_txt.dat", text_payload, 0);
  if (status == OS_STATUS_OK) {
    out_len = 999u;
    status = os_fs_read_file_bytes("binfs_txt.dat", read_buf, sizeof(read_buf),
                                   &out_len);
    if (status == OS_STATUS_OK && out_len == 10u &&
        bytes_equal(read_buf, text_payload, 10u)) {
      app_log("BINFS:PASS:text_len_compat\n");
    } else {
      app_log("BINFS:FAIL:text_len_compat\n");
      failures++;
    }
  } else {
    app_log("BINFS:FAIL:text_len_compat:write\n");
    failures++;
  }

  if (failures == 0) {
    app_log("BINFS:done\n");
  } else {
    app_log("BINFS:incomplete\n");
  }
  return 0;
}
