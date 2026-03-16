/**
 * @file main.c
 * @brief Host-side CLI tool for wrapping raw ELF files into SOF format.
 *
 * Purpose:
 *   Reads a raw ELF binary from disk, wraps it in a SecureOS File Format
 *   (SOF) container with user-specified metadata, and writes the result
 *   to an output file.  Used by the build pipeline to convert compiled
 *   ELF binaries into .bin (executable) or .lib (library) SOF files.
 *
 * Usage:
 *   sof_wrap --type bin --name "app" --author "SecureOS" --version "1.0.0" \
 *            --date "2026-03-16" [--description "desc"] [--icon "icon"] \
 *            input.elf output.bin
 *
 * Interactions:
 *   - kernel/format/sof.h and kernel/format/sof.c provide the SOF build API.
 *
 * Launched by:
 *   Called by build/scripts/build_user_app.sh and build_user_lib.sh after
 *   the linker produces the raw ELF output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/format/sof.h"

enum {
  SOF_WRAP_MAX_FILE_SIZE = 65536,
  SOF_WRAP_MAX_OUTPUT_SIZE = 65536 + 4096,
};

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s --type <bin|lib> --name <name> [--author <author>]\n"
          "       [--version <version>] [--date <date>] [--description <desc>]\n"
          "       [--icon <icon>] <input.elf> <output.bin|output.lib>\n",
          prog);
}

int main(int argc, char **argv) {
  const char *type_str = NULL;
  const char *name = NULL;
  const char *author = NULL;
  const char *version = NULL;
  const char *date = NULL;
  const char *description = NULL;
  const char *icon = NULL;
  const char *input_path = NULL;
  const char *output_path = NULL;
  sof_file_type_t file_type = SOF_TYPE_INVALID;
  FILE *fp = NULL;
  uint8_t *elf_data = NULL;
  uint8_t *sof_data = NULL;
  size_t elf_size = 0;
  size_t sof_size = 0;
  sof_build_params_t params;
  sof_result_t result;
  int i;

  /* Parse arguments */
  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
      type_str = argv[++i];
    } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
      name = argv[++i];
    } else if (strcmp(argv[i], "--author") == 0 && i + 1 < argc) {
      author = argv[++i];
    } else if (strcmp(argv[i], "--version") == 0 && i + 1 < argc) {
      version = argv[++i];
    } else if (strcmp(argv[i], "--date") == 0 && i + 1 < argc) {
      date = argv[++i];
    } else if (strcmp(argv[i], "--description") == 0 && i + 1 < argc) {
      description = argv[++i];
    } else if (strcmp(argv[i], "--icon") == 0 && i + 1 < argc) {
      icon = argv[++i];
    } else if (argv[i][0] != '-' && input_path == NULL) {
      input_path = argv[i];
    } else if (argv[i][0] != '-' && output_path == NULL) {
      output_path = argv[i];
    } else {
      fprintf(stderr, "Unknown argument: %s\n", argv[i]);
      usage(argv[0]);
      return 1;
    }
  }

  if (type_str == NULL || name == NULL || input_path == NULL || output_path == NULL) {
    fprintf(stderr, "Error: --type, --name, input, and output are required.\n");
    usage(argv[0]);
    return 1;
  }

  if (strcmp(type_str, "bin") == 0) {
    file_type = SOF_TYPE_BIN;
  } else if (strcmp(type_str, "lib") == 0) {
    file_type = SOF_TYPE_LIB;
  } else {
    fprintf(stderr, "Error: --type must be 'bin' or 'lib'.\n");
    return 1;
  }

  /* Read input ELF file */
  fp = fopen(input_path, "rb");
  if (fp == NULL) {
    fprintf(stderr, "Error: cannot open input file '%s'.\n", input_path);
    return 1;
  }

  fseek(fp, 0, SEEK_END);
  elf_size = (size_t)ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (elf_size == 0 || elf_size > SOF_WRAP_MAX_FILE_SIZE) {
    fprintf(stderr, "Error: input file size %zu out of range (1..%d).\n",
            elf_size, SOF_WRAP_MAX_FILE_SIZE);
    fclose(fp);
    return 1;
  }

  elf_data = (uint8_t *)malloc(elf_size);
  if (elf_data == NULL) {
    fprintf(stderr, "Error: cannot allocate %zu bytes for ELF data.\n", elf_size);
    fclose(fp);
    return 1;
  }

  if (fread(elf_data, 1, elf_size, fp) != elf_size) {
    fprintf(stderr, "Error: failed to read input file.\n");
    free(elf_data);
    fclose(fp);
    return 1;
  }
  fclose(fp);

  /* Build SOF container */
  sof_data = (uint8_t *)malloc(SOF_WRAP_MAX_OUTPUT_SIZE);
  if (sof_data == NULL) {
    fprintf(stderr, "Error: cannot allocate output buffer.\n");
    free(elf_data);
    return 1;
  }

  memset(&params, 0, sizeof(params));
  params.file_type = file_type;
  params.name = name;
  params.description = description;
  params.author = author;
  params.version = version;
  params.date = date;
  params.icon = icon;
  params.elf_payload = elf_data;
  params.elf_payload_size = elf_size;

  result = sof_build(&params, sof_data, SOF_WRAP_MAX_OUTPUT_SIZE, &sof_size);
  if (result != SOF_OK) {
    fprintf(stderr, "Error: sof_build failed with code %d.\n", (int)result);
    free(sof_data);
    free(elf_data);
    return 1;
  }

  /* Write output SOF file */
  fp = fopen(output_path, "wb");
  if (fp == NULL) {
    fprintf(stderr, "Error: cannot open output file '%s'.\n", output_path);
    free(sof_data);
    free(elf_data);
    return 1;
  }

  if (fwrite(sof_data, 1, sof_size, fp) != sof_size) {
    fprintf(stderr, "Error: failed to write output file.\n");
    fclose(fp);
    free(sof_data);
    free(elf_data);
    return 1;
  }

  fclose(fp);
  free(sof_data);
  free(elf_data);

  printf("sof_wrap: %s -> %s (%zu bytes, type=%s)\n",
         input_path, output_path, sof_size, type_str);
  return 0;
}