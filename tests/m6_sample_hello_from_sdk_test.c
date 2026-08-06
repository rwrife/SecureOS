/**
 * @file m6_sample_hello_from_sdk_test.c
 * @brief Host fixture verifier for samples/hello-from-sdk (issue #584 starter).
 *
 * Purpose:
 *   Verifies two starter-slice invariants for the M6-SDK-004 sample:
 *     1. Source/include surface remains SDK-only (`os/abi.h`, no kernel or
 *        in-tree private header reach-through).
 *     2. Manifest remains valid for external-app posture (`owner.kind=external`
 *        with CAP_CONSOLE_WRITE requested).
 *
 * Inputs:
 *   argv[1] = sample source path (`samples/hello-from-sdk/main.c`)
 *   argv[2] = sample manifest path (`samples/hello-from-sdk/manifest.json`)
 *   argv[3] = nm dump path emitted by build_sample_hello_from_sdk.sh
 *
 * Launched by:
 *   build/scripts/test_m6_sample_hello_from_sdk.sh
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail_reason(const char *reason) {
  printf("TEST:FAIL:m6_sample_hello_from_sdk:%s\n", reason);
}

static char *read_all(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long size = ftell(f);
  if (size < 0) {
    fclose(f);
    return NULL;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  char *buf = (char *)malloc((size_t)size + 1u);
  if (buf == NULL) {
    fclose(f);
    return NULL;
  }

  size_t got = fread(buf, 1, (size_t)size, f);
  fclose(f);
  if (got != (size_t)size) {
    free(buf);
    return NULL;
  }
  buf[size] = '\0';
  if (out_len != NULL) {
    *out_len = (size_t)size;
  }
  return buf;
}

static bool contains(const char *haystack, const char *needle) {
  return strstr(haystack, needle) != NULL;
}

static bool include_line_has(const char *text, const char *needle) {
  const char *cursor = text;
  while (*cursor != '\0') {
    const char *line_end = strchr(cursor, '\n');
    size_t len = line_end != NULL ? (size_t)(line_end - cursor) : strlen(cursor);

    if (len > 0u && strstr(cursor, "#include") != NULL) {
      if (strstr(cursor, needle) != NULL) {
        return true;
      }
    }

    if (line_end == NULL) {
      break;
    }
    cursor = line_end + 1;
  }
  return false;
}

static int verify_nm_no_undefined_symbols(const char *nm_path) {
  FILE *f = fopen(nm_path, "r");
  if (f == NULL) {
    fail_reason("nm_dump_missing");
    return 1;
  }

  char line[512];
  while (fgets(line, sizeof(line), f) != NULL) {
    const char *u = strstr(line, " U ");
    if (u != NULL) {
      fclose(f);
      fail_reason("undefined_symbol_present");
      return 1;
    }
  }

  fclose(f);
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fail_reason("usage_source_manifest_nm_required");
    return 1;
  }

  printf("TEST:START:m6_sample_hello_from_sdk\n");

  const char *source_path = argv[1];
  const char *manifest_path = argv[2];
  const char *nm_path = argv[3];

  size_t source_len = 0;
  char *source = read_all(source_path, &source_len);
  if (source == NULL || source_len == 0u) {
    fail_reason("source_read_failed");
    free(source);
    return 1;
  }

  if (!include_line_has(source, "os/abi.h")) {
    fail_reason("sdk_header_missing");
    free(source);
    return 1;
  }
  if (include_line_has(source, "secureos_api.h") ||
      include_line_has(source, "kernel/") ||
      include_line_has(source, "user/include/")) {
    fail_reason("non_sdk_header_reference_found");
    free(source);
    return 1;
  }

  if (verify_nm_no_undefined_symbols(nm_path) != 0) {
    free(source);
    return 1;
  }

  printf("TEST:PASS:m6_sample_hello_from_sdk:builds_against_sdk_only\n");

  size_t manifest_len = 0;
  char *manifest = read_all(manifest_path, &manifest_len);
  if (manifest == NULL || manifest_len == 0u) {
    fail_reason("manifest_read_failed");
    free(source);
    free(manifest);
    return 1;
  }

  if (!contains(manifest, "\"kind\": \"external\"")) {
    fail_reason("manifest_owner_kind_external_missing");
    free(source);
    free(manifest);
    return 1;
  }
  if (!contains(manifest, "CAP_CONSOLE_WRITE")) {
    fail_reason("manifest_console_cap_missing");
    free(source);
    free(manifest);
    return 1;
  }

  printf("TEST:PASS:m6_sample_hello_from_sdk:manifest_validates\n");
  printf("TEST:PASS:m6_sample_hello_from_sdk\n");

  free(source);
  free(manifest);
  return 0;
}
