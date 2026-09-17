/**
 * @file main.c
 * @brief Binary-safe filesystem I/O closeout evidence application (#765).
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
 *     - denied_write_no_mutation: a user-denied overwrite returns
 *       DENIED and leaves the previously stored bytes unchanged.
 *     - notfound:       a missing file reports NOT_FOUND (distinct from
 *       an empty-file EOF) with out_len pinned to 0.
 *     - text_len_compat: the legacy text write path followed by the
 *       bytes read path reports the exact stored byte count, proving
 *       existing text callers keep their behavior.
 *     - archive_roundtrip: reads the app's own on-image SOF runtime
 *       archive (/apps/binfs.bin — the exact container the launcher
 *       loads it from), persists it to a new file (binarc.dat), and
 *       re-reads that copy for a byte-exact comparison. The app binary
 *       is guaranteed <= 64 KiB because the launcher itself loads app
 *       candidates through a 64 KiB staging buffer (APP_FILE_MAX), so
 *       the 64 KiB read buffer is a proven-safe upper bound, not an
 *       assumption. This is the "read a real runtime archive and
 *       persist an ELF/SOF byte-for-byte" acceptance clause of #765.
 *     - persistence_256k: writes a 256 KiB + 256 byte generated payload
 *       (deterministic LCG bytes, forced NUL every 4096th byte,
 *       high-bit bytes throughout) spanning ~513 FAT clusters, re-reads
 *       it into the same buffer, and stream-compares against a
 *       re-seeded regeneration. 256 KiB clears the legacy 64 KiB
 *       single-buffer ceiling (APP_FILE_MAX) the issue names as the
 *       fixed limit to remove or accommodate, and stays inside a safe
 *       disk-image cluster budget for the seeded FAT volume.
 *
 * Memory shape:
 *   All payload buffers are static (image .bss), not heap: the app
 *   intentionally makes no os_mem_brk call so the default 64 KiB
 *   manifest arena plays no role in the gate. Combined static payload
 *   (~321 KiB) plus code stays inside the native load window
 *   [0x800000, APP_NATIVE_BRIDGE_ADDR) via p_memsz, while the ELF file
 *   itself (p_filesz, what the launcher's 64 KiB load buffer holds)
 *   stays small because .bss occupies no file bytes.
 *
 * Buffer-reuse discipline:
 *   archive_roundtrip keeps its source copy in arc_buf while comparing
 *   the persisted copy in big_buf (different sizes, no aliasing).
 *   persistence_256k generates into big_buf, writes, re-reads the file
 *   back over big_buf, and compares against an on-the-fly reseeded
 *   regeneration, so no second large buffer is needed.
 *
 * Interactions:
 *   - secureos_api.h: the only OS surface used. Console output goes
 *     through os_console_write; every file operation goes through the
 *     binary-safe wrappers and inherits the launcher's capability and
 *     consent gates (each operation triggers the console's disk-IO
 *     authorization prompt unless an always decision was cached).
 *
 * Launched by:
 *   The `kernel_binfs` QEMU gate (build/scripts/run_qemu.sh --test
 *   kernel_binfs), which boots the ordinary ISO + seeded disk image,
 *   answers the 16 disk-IO consent prompts (15 allows, one deny on the
 *   second binnul.dat write), and asserts the BINFS markers plus a
 *   clean `exit pass`. Built by build/scripts/build_user_app.sh and
 *   staged at /apps/binfs.bin by build/scripts/build_disk_image.sh.
 *
 * This gate carries the runtime-archive read + persist and the
 * multi-cluster 256 KiB persistence clauses of #765. Building/running a
 * real freestanding compiler and its own ELF emission remain the scope
 * of #766/#767.
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

/* Deterministic byte at index i for the large-payload persistence
 * evidence: a fixed-seed LCG with bit 7 forced on (so a truncating or
 * string-based path can never fake a match with an empty/short read)
 * plus a forced NUL every 4096 bytes (so the payload keeps exercising
 * embedded-zero survival at scale). Generation is pure function of the
 * index, which lets the compare pass reseed and regenerate instead of
 * keeping a second copy alive. */
#define BIG_PAYLOAD_LEN (256u * 1024u + 256u)
static unsigned char big_buf[BIG_PAYLOAD_LEN];
/* Upper bound for a launchable app archive: the launcher itself stages
 * app candidates through a 64 KiB buffer (APP_FILE_MAX), so anything
 * that can boot is at most that big. Used for archive_roundtrip. */
#define ARCHIVE_BUF_LEN 65536u
static unsigned char arc_buf[ARCHIVE_BUF_LEN];

static unsigned char payload_byte_at(unsigned int i) {
  unsigned int state = 17459u + i;
  unsigned int k;
  if ((i % 4096u) == 0u) {
    return 0x00u;
  }
  for (k = 0u; k < 3u; ++k) {
    state = state * 1103515245u + 12345u;
  }
  return (unsigned char)(((state >> 16) & 0x7Fu) | 0x80u);
}

static void payload_fill(unsigned char *buf, unsigned int len) {
  unsigned int i;
  for (i = 0u; i < len; ++i) {
    buf[i] = payload_byte_at(i);
  }
}

int main(void) {
  /* Leading, interior, and trailing NULs plus high-bit bytes — the
   * exact payload classes the old strnlen-based fd path corrupted. */
  static const unsigned char payload_nul[12] = {
      0x00u, 0x01u, 0x02u, 0x00u, 0x04u, 0xFFu,
      0x80u, 0x00u, 0x7Fu, 0x41u, 0x00u, 0x00u};
  static const unsigned char payload_a[4] = {0x00u, 0xFEu, 0x00u, 0x01u};
  static const unsigned char payload_b[3] = {0x00u, 0x00u, 0xFDu};
  static const unsigned char denied_payload[3] = {0xDEu, 0xADu, 0x00u};
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

  /* 2) denied_write_no_mutation: deny an overwrite, then prove that the
   *    original NUL-heavy payload is still present byte-for-byte. */
  status = os_fs_write_file_bytes("binnul.dat", denied_payload,
                                  sizeof(denied_payload), 0);
  if (status == OS_STATUS_DENIED) {
    out_len = 999u;
    status = os_fs_read_file_bytes("binnul.dat", read_buf, sizeof(read_buf),
                                   &out_len);
    if (status == OS_STATUS_OK && out_len == sizeof(payload_nul) &&
        bytes_equal(read_buf, payload_nul, sizeof(payload_nul))) {
      app_log("BINFS:PASS:denied_write_no_mutation\n");
    } else {
      app_log("BINFS:FAIL:denied_write_no_mutation:readback\n");
      failures++;
    }
  } else {
    app_log("BINFS:FAIL:denied_write_no_mutation:status\n");
    failures++;
  }

  /* 3) append_bytes: create then append, both carrying NUL bytes. */
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

  /* 4) capacity_error: undersized read fails cleanly with out_len 0. */
  out_len = 999u;
  status = os_fs_read_file_bytes("binnul.dat", tiny, sizeof(tiny), &out_len);
  if (status == OS_STATUS_ERROR && out_len == 0u) {
    app_log("BINFS:PASS:capacity_error\n");
  } else {
    app_log("BINFS:FAIL:capacity_error\n");
    failures++;
  }

  /* 5) notfound: missing file is NOT_FOUND with out_len 0, distinct
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

  /* 6) text_len_compat: legacy text write stores exactly the string
   *    bytes; the bytes-read path reports that exact length. */
  status = os_fs_write_file("bintxt.dat", text_payload, 0);
  if (status == OS_STATUS_OK) {
    out_len = 999u;
    status = os_fs_read_file_bytes("bintxt.dat", read_buf, sizeof(read_buf),
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

  /* 7) archive_roundtrip: read the app's own signed SOF runtime archive
   *    from /apps/binfs.bin (the exact container the launcher loaded it
   *    from), persist it byte-for-byte to binarc.dat, then re-read the
   *    persisted copy and compare. This is the #765 acceptance clause
   *    "read a real runtime archive and persist an ELF/SOF
   *    byte-for-byte", evidenced on the guest path. A byte-identical
   *    re-read is a strict SUPERSET of a header-only check: any
   *    corruption of embedded NULs, truncation, or length loss fails
   *    the compare. The launcher guarantees any bootable app binary
   *    fits ARCHIVE_BUF_LEN because it stages app candidates through a
   *    same-size buffer before exec. */
  out_len = 999u;
  status = os_fs_read_file_bytes("/apps/binfs.bin", arc_buf,
                                 ARCHIVE_BUF_LEN, &out_len);
  if (status == OS_STATUS_OK && out_len > 0u) {
    unsigned int archive_len = out_len;
    status = os_fs_write_file_bytes("binarc.dat", arc_buf, archive_len, 0);
    if (status == OS_STATUS_OK) {
      out_len = 999u;
      status = os_fs_read_file_bytes("binarc.dat", big_buf, BIG_PAYLOAD_LEN,
                                     &out_len);
      if (status == OS_STATUS_OK && out_len == archive_len &&
          bytes_equal(big_buf, arc_buf, archive_len)) {
        app_log("BINFS:PASS:archive_roundtrip\n");
      } else {
        app_log("BINFS:FAIL:archive_roundtrip\n");
        failures++;
      }
    } else {
      app_log("BINFS:FAIL:archive_roundtrip:write\n");
      failures++;
    }
  } else {
    app_log("BINFS:FAIL:archive_roundtrip:read\n");
    failures++;
  }

  /* 8) persistence_256k: the "sizes required by the demo" clause.
   *    Writes a 256 KiB + 256 byte deterministic payload (embedded
   *    NULs every 4096 bytes, high-bit bytes throughout — the classes
   *    string-based paths corrupt) spanning ~513 FAT clusters, then
   *    re-reads it whole and stream-compares against a reseeded
   *    regeneration of the generator (no second large buffer alive).
   *    Clears the legacy 64 KiB APP_FILE_MAX single-buffer ceiling the
   *    issue calls out; proves the FAT chain writer + length-bearing
   *    reader hold multi-cluster binary data byte-exact. */
  payload_fill(big_buf, BIG_PAYLOAD_LEN);
  status = os_fs_write_file_bytes("binbig.dat", big_buf, BIG_PAYLOAD_LEN, 0);
  if (status == OS_STATUS_OK) {
    unsigned int i;
    int mismatch = 0;
    out_len = 999u;
    status = os_fs_read_file_bytes("binbig.dat", big_buf, BIG_PAYLOAD_LEN,
                                   &out_len);
    if (status == OS_STATUS_OK && out_len == BIG_PAYLOAD_LEN) {
      for (i = 0u; i < BIG_PAYLOAD_LEN; ++i) {
        if (big_buf[i] != payload_byte_at(i)) {
          mismatch = 1;
          break;
        }
      }
    } else {
      mismatch = 1;
    }
    if (mismatch == 0) {
      app_log("BINFS:PASS:persistence_256k\n");
    } else {
      app_log("BINFS:FAIL:persistence_256k\n");
      failures++;
    }
  } else {
    app_log("BINFS:FAIL:persistence_256k:write\n");
    failures++;
  }

  if (failures == 0) {
    app_log("BINFS:done\n");
    (void)os_process_exit(0);
  } else {
    app_log("BINFS:incomplete\n");
    (void)os_process_exit(1);
  }
  /* The in-guest process-exit bridge does not return. Preserve an accurate
   * fallback status for host/static builds where no bridge is attached. */
  return failures == 0 ? 0 : 1;
}
