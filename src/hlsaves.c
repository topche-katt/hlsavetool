#include "oodle.h"

static const byte needle[] = {
  0x11, 0x00, 0x00, 0x00, // length
  0x52, 0x61, 0x77, 0x44, // RawDatabaseImage string
  0x61, 0x74, 0x61, 0x62,
  0x61, 0x73, 0x65, 0x49,
  0x6D, 0x61, 0x67, 0x65,
  0x00 // null terminator
};
const enum { needle_len = sizeof(needle) };

void usage(const char *argv[]) {
  const char *basename = strrchr(argv[0], '\\');
  basename = basename != NULL ? basename + 1 : APPLICATION_IMAGE_NAME;
  printf("Usage: %s [OPTION] input output [VERBOSE]\n [OPTION]\n  -d decompress converts new to old format\n  -c compress converts old to new format\n [VERBOSE]\n  -v prints additional info (optional)\n", basename);
}

int main(const int argc, const char *argv[]) {
  printf("Hogwarts Legacy save file tool - decompress/compress RawDatabaseImage SQLite database.\n"
    "Open source tool by @katt and @ifonlythatweretrue\n"
    "If you face issues, run the tool with verbosity flag \"-v\" and submit us a ticket (attach save file & tool output)\n"
    "Report issues at https://github.com/topche-katt/hlsavetool/issues.\n\n"
  );

  if (argc < 4) {
    usage(argv);
    exit(EXIT_FAILURE);
  }

  const char *command = argv[1];
  if (memcmp(command, COMMAND_DECOMPRESS, strlen(COMMAND_DECOMPRESS)) != 0
    && memcmp(command, COMMAND_COMPRESS, strlen(COMMAND_COMPRESS)) != 0
  ) {
    printf_error("Unknown command \"%s\"", command);
    usage(argv);
    exit(EXIT_FAILURE);
  }

  uint8_t command_decompress = memcmp(command, COMMAND_DECOMPRESS, strlen(COMMAND_DECOMPRESS));
  uint8_t command_compress = memcmp(command, COMMAND_COMPRESS, strlen(COMMAND_COMPRESS));

  FILE *fpin = NULL;
  const char *input_filename = argv[2];
  OPEN_FILE_WITH_ERROR_HANDLE(input_filename, "rb", fpin);

  FILE *fpout = NULL;
  const char *output_filename = argv[3];
  OPEN_FILE_WITH_ERROR_HANDLE(output_filename, "wb", fpout);

  bool verbose = (argc == 5 && argv[4] != NULL) ? strcmp(VERBOSITY_FLAG, argv[4]) == 0 : false;

  printf("Trying to %s save file \"%s\"\n", command_compress == 0 ? "compress" : "decompress", input_filename);

  printf_verbose(verbose, "Input file: %s", input_filename);
  printf_verbose(verbose, "Output file: %s", output_filename);

  size_t file_size = 0;
  fseek(fpin, 0, SEEK_END);
  file_size = ftell(fpin);
  fseek(fpin, 0, SEEK_SET);
  printf_verbose(verbose, "Input file size: %llu bytes", file_size);

  size_t buffer_size = sizeof (byte) * file_size;
  printf_verbose(verbose, "Input file memory buffer size: %llu bytes", buffer_size);

  SAFE_ALLOC_SIZE(byte, buffer, buffer_size);
  const size_t read_size = fread(buffer, buffer_size, 1, fpin);
  if (read_size != 1 && (feof(fpin) || ferror(fpin))) {
    printf_error("Input file: %llu = fread(buffer, buffer_size, 1, fp); failed", read_size);
    return EXIT_FAILURE;
  }

  GvasHeader header;
  memcpy(&header, buffer, sizeof (header));

  if (header.signature != GVAS_HEADER_SIGNATURE) {
    printf_error("Invalid GVAS header signature, expected 0x%08X got 0x%08X", _byteswap_ulong(GVAS_HEADER_SIGNATURE), _byteswap_ulong(header.signature));
    return EXIT_FAILURE;
  }

  if (header.version != GVAS_HEADER_VERSION) {
    printf_error("Invalid GVAS header version, expected %d got %d", GVAS_HEADER_VERSION, header.version);
    return EXIT_FAILURE;
  }

  printf_verbose(verbose, "GVAS File Header:");
  printf_verbose(verbose, " Signature: 0x%08X", _byteswap_ulong(header.signature));
  printf_verbose(verbose, " Version: %d", header.version);
  printf_verbose(verbose, " Package: %d", header.package);
  printf_verbose(verbose, " Engine: %d.%d.%d (%lu)", header.engine.major, header.engine.minor, header.engine.patch, header.engine.changelist);

  MemoryAddress rdi = {
    .address = buffer,
    .size = buffer_size
  };
  byte *address = NULL;
  SEARCH_MEMORY(address, rdi.address, rdi.size, needle, needle_len);

  if (address == NULL) {
    printf_error("Could not locate \"RawDatabaseImage\" UProperty");
    exit(EXIT_FAILURE);
  }

  printf_verbose(verbose, "Address of Base: %llu", (uint64_t) buffer);
  printf_verbose(verbose, "Address of RawDatabaseImage: %llu", (uint64_t) address);

  rdi.address = address;
  MemoryAddress head = {
    .address = buffer,
    .size = (address - buffer)
  };
  printf_verbose(verbose, "Head address %llu and size %llu bytes", (uint64_t) head.address, head.size);
  printf_verbose(verbose, "Head relative offset: %llu", address - buffer);

  SAFE_ALLOC(UProperty, property);

  READ_FSTRING(property->name, address);
  PRINT_FSTRING(property->name);
  if (memcmp(property->name.data, RDI_UPROPERTY_NAME, RDI_UPROPERTY_NAME_LEN) != 0) {
    printf_error("Expected UProperty with name \"%s\" got \"%s\"", RDI_UPROPERTY_NAME, (byte *) property->name.data);
    exit(EXIT_FAILURE);
  }

  READ_FSTRING(property->type, address);
  PRINT_FSTRING(property->type);
  if (memcmp(property->type.data, RDI_UPROPERTY_TYPE, RDI_UPROPERTY_TYPE_LEN) != 0) {
    printf_error("Expected UProperty of type \"%s\" got \"%s\"", RDI_UPROPERTY_TYPE, (byte *) property->type.data);
    exit(EXIT_FAILURE);
  }

  COPY_MEMORY(address, &property->length, sizeof (property->length));
  printf_verbose(verbose, "ArrayProperty value length: %llu bytes", property->length);

  SAFE_ALLOC(UArrayProperty, value);

  READ_FSTRING(value->type, address);
  PRINT_FSTRING(value->type);
  if (memcmp(value->type.data, RDI_UPROPERTY_VALUE_TYPE, RDI_UPROPERTY_VALUE_TYPE_LEN) != 0) {
    printf_error("Expected UProperty data of type \"%s\" got \"%s\"", RDI_UPROPERTY_VALUE_TYPE, (byte *) value->type.data);
    exit(EXIT_FAILURE);
  }

  COPY_MEMORY(address, &value->unknown, sizeof (value->unknown));
  COPY_MEMORY(address, &value->size, sizeof (value->size));
  printf_verbose(verbose, "ByteProperty value length: %lu bytes", value->size);

  size_t size = sizeof (byte) * value->size;
  SAFE_ALLOC_SIZE(byte, data, size);
  COPY_MEMORY(address, data, size);
  value->value = data;
  property->data = value;

  uint32_t signature = 0;
  memcpy(&signature, data, 4);
  printf_verbose(verbose, "ByteProperty value signature: 0x%08X", _byteswap_ulong(signature));

  rdi.size = property->length + RDI_UPROPERTY_DATA_OFFSET;
  MemoryAddress tail = {
    .address = buffer + head.size + property->length + RDI_UPROPERTY_DATA_OFFSET,
    .size = buffer_size - head.size - property->length - RDI_UPROPERTY_DATA_OFFSET
  };
  printf_verbose(verbose, "Tail address %llu and size %llu bytes", (uint64_t) tail.address, tail.size);
  printf_verbose(verbose, "Tail relative offset: %llu", tail.address - buffer);
  printf_verbose(verbose, "Before processing %s with %s command", (byte *) property->name.data, command);

  if (command_decompress == 0) {
    decompress(property, verbose);
  }

  if (command_compress == 0) {
    compress(property, verbose);
  }

  printf_verbose(verbose, "Begin writing to output file: %s", output_filename);

  WRITE_FILE_WITH_ERROR_HANDLE(fpout, head.address, head.size, 1);
  SERIALIZE_ARRAY_PROPERTY(property, fpout);
  WRITE_FILE_WITH_ERROR_HANDLE(fpout, tail.address, tail.size, 1);

  printf_verbose(verbose, "Finished writing to output file: %s", output_filename);

  fclose(fpout);
  fclose(fpin);

  free(buffer);
  free(value->value);
  free(value);
  free(property);

  printf("Successfully %s to save file \"%s\"\n", command_compress == 0 ? "compressed" : "decompressed", output_filename);

  return EXIT_SUCCESS;
}

/**
 * Quick memmem() implementation that runs on Windows...
 */
void *memmem(const void *haystack, size_t haystack_len, const void *needle, const size_t needle_len) {
  if (haystack == NULL || needle == NULL || haystack_len == 0 || needle_len == 0 || haystack_len < needle_len) {
    return NULL;
  }

  for (const char *chr = haystack; haystack_len >= needle_len; ++chr, --haystack_len) {
    if (!memcmp(chr, needle, needle_len)) {
      return (void *) chr;
    }
  }

  return NULL;
}

/**
 * Print error message wrapper
 */
int printf_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  printf("\x1B[31m[Error] ");
  int result = vprintf(format, args);
  printf(".\x1B[0m\n");
  va_end(args);

  return result;
}

/**
 * Print verbose text (if verbosity is enabled) wrapper
 */
int printf_verbose(bool enabled, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = 0;
  if (enabled) {
    printf("\x1B[34m[Info] ");
    result = vprintf(format, args);
    printf("\x1B[0m\n");
  }
  va_end(args);

  return result;
}
