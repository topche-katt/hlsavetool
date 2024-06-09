#pragma once

#ifdef NDEBUG
  #define dprintf
#else
  #define dprintf printf
#endif

#pragma pack(push, 1)

// Useful wrapper for memmem() calls
typedef struct _MEMORY_ADDRESS {
  byte *address;
  size_t size;
} MemoryAddress;

// GVAS file header (important info)
typedef struct _GVAS_HEADER {
  uint32_t signature;
  uint32_t version;
  uint32_t package;
  struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint32_t changelist; // mask `& 0x7fffffffu` and licensee `& 0x80000000u != 0u`
  } engine;
} GvasHeader;

/**
 * NOTE: Pascal style strings with Complex logic
 *  - positive length for ANSI
 *  - negative for UCS2
 *  - length of 0 indicates NULL
 *  - length of 1 indicates EMPTY FString
 */
typedef struct _USTRING {
  uint32_t length;
  void *data;
} FString;

// Generic UProperty format
typedef struct _UProperty {
  FString name;
  FString type;
  uint64_t length;
  void *data;
} UProperty;

// Specific ByteArray UProperty format
typedef struct _UProperty_Array {
  FString type;
  uint8_t unknown;
  uint32_t size;
  void *value;
} UArrayProperty;
#pragma pack(pop)

// Expected GVAS file signature and version
#define GVAS_HEADER_SIGNATURE 0x53415647
#define GVAS_HEADER_VERSION 2

// Supported commands
#define COMMAND_DECOMPRESS "-d"
#define COMMAND_COMPRESS "-c"
#define VERBOSITY_FLAG "-v"

// Sick of duplicated string literals
#define APPLICATION_IMAGE_NAME "hlsaves.exe"

// UProperty names and lengths
#define RDI_UPROPERTY_NAME "RawDatabaseImage"
#define RDI_UPROPERTY_NAME_LEN 17
#define RDI_UPROPERTY_TYPE "ArrayProperty"
#define RDI_UPROPERTY_TYPE_LEN 14
#define RDI_UPROPERTY_VALUE_TYPE "ByteProperty"
#define RDI_UPROPERTY_VALUE_TYPE_LEN 13

// Offset of UProperty base
#define RDI_UPROPERTY_DATA_OFFSET 65

// UArrayProperty size is data + 4
#define UARRAYPROPERTY_ADDED_LENGTH 4

// Multiplication factor for UArrayProperty size
#define OODLE_COMPRESSION_FACTOR_MAGIC 15

// Oodle UPK signature
#define OODLE_MAX_BLOCK_SIZE 131072
#define OODLE_COMPRESSED_BLOCK_SIGNATURE 0x9E2A83C1
#define OODLE_DLL_FILENAME "oo2core_9_win64.dll"

// SQLite header signature info
#define SQLITE_HEADER_SIGNATURE "SQLite format 3"
#define SQLITE_HEADER_SIGNATURE_LEN 16

// Upk header prepended to compressed SQLite + 4
#define SQLITE_UPK_HEADER_ADDED_LENGTH 4

/**
 * Obtain procedure address or error out on failure
 */
#define GET_PROCEDURE_ADDRESS(handle, address, type, name) do { \
  address = (type *) GetProcAddress(handle, name); \
  if (address == NULL) { \
    printf_error("Failed to obtain %s procedure address", #type); \
    exit(EXIT_FAILURE); \
  } \
} while (0)

/**
 * Safely allocate memory and zero it out
 */
#define SAFE_ALLOC(type, pointer) SAFE_ALLOC_SIZE(type, pointer, sizeof (type))
#define SAFE_ALLOC_SIZE(type, pointer, size) \
type *pointer = NULL; \
do { \
    pointer = (type *) malloc(size); \
    if (pointer == NULL) { \
      printf_error("%s *%s = (%s *) malloc(%s); failed to allocate %llu bytes with error(%d): %s", \
        #type, #pointer, #type, #size, sizeof (*pointer), errno, strerror(errno) \
      ); \
      exit(EXIT_FAILURE); \
    } \
    memset(pointer, 0, size); \
} while (0)

/**
 * Open a file or error out on failure
 */
#define OPEN_FILE_WITH_ERROR_HANDLE(filename, mode, file) do { \
  file = fopen(filename, mode); \
  if (file == NULL) { \
    printf_error("fopen(\"%s\"); failed with error(%d): %s", filename, errno, strerror(errno)); \
    exit(EXIT_FAILURE); \
  } \
} while (0)

/**
 * Verify fwrite operation was successful
 */
#define WRITE_FILE_WITH_ERROR_HANDLE(filename, data, size, count) do { \
  const size_t write_count = fwrite(data, size, count, filename); \
  if (write_count != count || ferror(filename)) { \
    printf_error("Writing to file failed: %llu = fwrite(data, %llu, %u, filename); failed", write_count, size, count); \
    return EXIT_FAILURE; \
  } \
} while (0)

/**
 * Search memory for a given data
 */
#define SEARCH_MEMORY(pointer, memory, size, data, length) do { \
  while (true) { \
    byte *found = memmem(memory, size, data, length); \
    if (!found) { break; } \
    pointer = found; \
    size -= (found + length) - memory; \
    memory = found + length; \
  } \
} while (0)

/**
 * Copy memory and advance the buffer pointer
 */
#define COPY_MEMORY(source, destination, size) do { \
  memcpy(destination, source, size); \
  source += size; \
} while (0)

/**
 * Read block of data into FString
 * TODO: Handle ASCI/UCS2 serialization
 */
#define READ_FSTRING(string, memory) do { \
  COPY_MEMORY(memory, &string.length, sizeof (string.length)); \
  if (string.length > 0) { \
    size_t data_size = sizeof (byte) * string.length; \
    SAFE_ALLOC_SIZE(byte, data, data_size); \
    COPY_MEMORY(memory, data, data_size); \
    string.data = data; \
  } \
} while (0)

/**
 * Printing FString for debugging purposes
 */
#define PRINT_FSTRING(string) do { \
  if (string.length > 0 && string.data != NULL) { \
    printf_verbose(verbose, "FString of length [%d] and value (%s)", string.length, (byte *) string.data); \
  } \
} while (0)

/**
 * Serialize FString
 */
#define SERIALIZE_FSTRING(string, file) do { \
  fwrite(&string.length, sizeof (string.length), 1, file); \
  fwrite(string.data, string.length, 1, file); \
} while (0)

/**
 * Ugly to look at...
 */
#define SERIALIZE_ARRAY_PROPERTY(property, file) do { \
  SERIALIZE_FSTRING(property->name, file); \
  SERIALIZE_FSTRING(property->type, file); \
  fwrite(&property->length, sizeof (property->length), 1, file); \
  UArrayProperty *array_property = (UArrayProperty *) property->data; \
  SERIALIZE_FSTRING(array_property->type, file); \
  fwrite(&array_property->unknown, sizeof (array_property->unknown), 1, file); \
  fwrite(&array_property->size, sizeof (array_property->size), 1, file); \
  fwrite((byte *) array_property->value, array_property->size, 1, file); \
} while (0)

/**
 * Windows does not support memmem(), so we do quick implementation of our own
 */
void *memmem(const void *haystack, size_t haystack_len, const void *needle, const size_t needle_len);

/**
 * Printf wrappers
 */
int printf_error(const char *format, ...);
int printf_verbose(bool enabled, const char *format, ...);
