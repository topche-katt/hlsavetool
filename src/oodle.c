#include "oodle.h"

static const byte signature[] = {
  0xC1, 0x83, 0x2A, 0x9E
};
const enum { signature_len = sizeof(signature) };

static HMODULE Oodle_Handle = NULL;
static OodleLZ_Compress_FP *OodleLZ_Compress = NULL;
static OodleLZ_Decompress_FP *OodleLZ_Decompress = NULL;
static OodleLZ_GetCompressedBufferSizeNeeded_FP *OodleLZ_Size_Needed = NULL;
static OodleLZ_CompressOptions_GetDefault_FP *OodleLZ_Compress_Options = NULL;

/**
 * Load dll and obtain function pointers or die a quick death...
 */
void InitOodleLibrary() {
  Oodle_Handle = LoadLibraryEx(OODLE_DLL_FILENAME, NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (Oodle_Handle == NULL) {
    printf_error("LoadLibraryEx(%s) failed with error code %lu", OODLE_DLL_FILENAME, GetLastError());
    exit(EXIT_FAILURE);
  }

  GET_PROCEDURE_ADDRESS(Oodle_Handle, OodleLZ_Compress, OodleLZ_Compress_FP, "OodleLZ_Compress");
  GET_PROCEDURE_ADDRESS(Oodle_Handle, OodleLZ_Decompress, OodleLZ_Decompress_FP, "OodleLZ_Decompress");
  GET_PROCEDURE_ADDRESS(Oodle_Handle, OodleLZ_Size_Needed, OodleLZ_GetCompressedBufferSizeNeeded_FP, "OodleLZ_GetCompressedBufferSizeNeeded");
  GET_PROCEDURE_ADDRESS(Oodle_Handle, OodleLZ_Compress_Options, OodleLZ_CompressOptions_GetDefault_FP, "OodleLZ_CompressOptions_GetDefault");
}

void releaseOodleLibrary() {
  if (Oodle_Handle != NULL) {
    FreeLibrary(Oodle_Handle);
  }

  OodleLZ_Compress = NULL;
  OodleLZ_Decompress = NULL;
  OodleLZ_Size_Needed = NULL;
  OodleLZ_Compress_Options = NULL;
}

void compress(UProperty *property, bool verbose) {
  UArrayProperty *data = (UArrayProperty *) property->data;

  printf_verbose(verbose, "Compressing %lu bytes of data...", data->size);
  InitOodleLibrary();

  byte *tmp_data = data->value;

  /**
   * Validate SQLite data before compressing
   */
  SqliteHeader sqlite_header;
  COPY_MEMORY(tmp_data, &sqlite_header, sizeof (sqlite_header));
  if (memcmp(sqlite_header.magic, SQLITE_HEADER_SIGNATURE, SQLITE_HEADER_SIGNATURE_LEN) != 0) {
    printf_error("Expected SQLite signature \"%s\" got \"%s\"", SQLITE_HEADER_SIGNATURE, sqlite_header.magic);
    exit(EXIT_FAILURE);
  }

  uint16_t sqlite_page_size = _byteswap_ushort(sqlite_header.page_size);
  uint32_t sqlite_database_size = _byteswap_ulong(sqlite_header.database_size);
  printf_verbose(verbose, " SQLite signature \"%s\", page size: %u and database size: %lu", sqlite_header.magic, sqlite_page_size, sqlite_database_size);

  uint32_t sqlite_size = sqlite_page_size * sqlite_database_size;
  printf_verbose(verbose, " SQLite calculate size: %lu", sqlite_size);

  if (sqlite_size != data->size) {
    printf_error("Expected sqlite database size (%lu) does not match actual size (%lu)", data->size, sqlite_size);
    exit(EXIT_FAILURE);
  }

  UpkOodleSqliteSize upk_sqlite_size = { sqlite_size + SQLITE_UPK_HEADER_ADDED_LENGTH, sqlite_size };

  /**
   * Output size should be less than input size,
   * just allocate uncompressed size of bytes
   */
  size_t result_size = 0;
  SAFE_ALLOC_SIZE(byte, result_data, sizeof (byte) * data->size);

  /**
   * NOTE: We need `UpkOodleSqliteSize` prepended to the SQLite data,
   * and compress the file in chunks of up to `OODLE_MAX_BLOCK_SIZE`
   * and wrap the compressed chunk in `UpkOodle`
   */
  size_t new_size = data->size + sizeof (upk_sqlite_size);
  byte *new_value = realloc(data->value, new_size);
  if (new_value == NULL) {
    printf_error("Reallocating memory of size %llu failed", new_size);
    exit(EXIT_FAILURE);
  }
  memmove(new_value + sizeof(upk_sqlite_size), data->value, data->size);
  memcpy(new_value, &upk_sqlite_size, sizeof (upk_sqlite_size));
  data->value = new_value;
  data->size = new_size;

  UpkOodle upk;
  memset(&upk, 0, sizeof (upk));
  upk.signature = OODLE_COMPRESSED_BLOCK_SIGNATURE;
  upk.max_block_size = OODLE_MAX_BLOCK_SIZE;

  OodleLZ_CompressOptions *constoptions = OodleLZ_Compress_Options(OodleLZ_Compressor_Kraken, OodleLZ_CompressionLevel_Fast);

  byte *tmp_data_value = data->value;
  size_t chunk_size = OODLE_MAX_BLOCK_SIZE;
  uint16_t chunk_index = 1;
  uint32_t pos = 0;
  do {
    /**
     * NOTE: Loop control must be first
     * thing in the LOOP BODY
     */
    pos += OODLE_MAX_BLOCK_SIZE;
    printf_verbose(verbose, "Raw Block #%u:", chunk_index);

    /**
     * If we are overshooting the buffer,
     * shrink expectations
     */
    if (pos > data->size) {
      chunk_size = OODLE_MAX_BLOCK_SIZE - (pos - data->size);
      printf_verbose(verbose, " Shrinking chunk #%u from %lu bytes to %llu bytes", chunk_index, OODLE_MAX_BLOCK_SIZE, chunk_size);
    }

    printf_verbose(verbose, " Scratch max size: %lu bytes", OODLE_MAX_BLOCK_SIZE);
    printf_verbose(verbose, " Uncompressed size: %llu bytes", chunk_size);

    uint64_t compressed_size_needed = (uint64_t) OodleLZ_Size_Needed(OodleLZ_Compressor_Kraken, chunk_size);
    printf_verbose(verbose, " Compressed size required: %llu bytes", compressed_size_needed);

    SAFE_ALLOC_SIZE(uint8_t, buffer, sizeof (uint8_t) * chunk_size);
    memcpy(buffer, tmp_data_value, chunk_size);

    SAFE_ALLOC_SIZE(uint8_t, output, sizeof (uint8_t) * compressed_size_needed);

    int compressed_bytes = OodleLZ_Compress(OodleLZ_Compressor_Kraken, buffer, chunk_size, output,
      OodleLZ_CompressionLevel_Fast, constoptions, NULL, NULL, NULL, 0
    );
    printf_verbose(verbose, " Compressed size: %lu bytes", compressed_bytes);

    /**
     * Wrap the compressed segment into `UpkOodle`
     */
    upk.blocks[0].compressed_size = compressed_bytes;
    upk.blocks[1].compressed_size = compressed_bytes;
    upk.blocks[0].uncompressed_size = chunk_size;
    upk.blocks[1].uncompressed_size = chunk_size;

    memcpy(result_data + result_size, &upk, sizeof (upk));
    result_size += sizeof (upk);
    memcpy(result_data + result_size, output, compressed_bytes);
    result_size += compressed_bytes;

    free(buffer);
    free(output);

    chunk_index++;
    tmp_data_value += chunk_size;
  } while (pos < data->size);

  /**
   * NOTE: UArrayProperty has + `UPROPERTY_ADDED_LENGTH`
   * bytes added to its length (pointer to embedded TYPE property??)
   */
  free(data->value);
  data->size = result_size;
  data->value = result_data;
  property->length = data->size + UARRAYPROPERTY_ADDED_LENGTH;

  releaseOodleLibrary();
}

void decompress(UProperty *property, bool verbose) {
  UArrayProperty *data = (UArrayProperty *) property->data;

  printf_verbose(verbose, "Decompressing %lu bytes of data...", data->size);
  InitOodleLibrary();

  /**
   * Do not even bother with realloc... just malloc big enough
   * chunk of memory and forget...
   *
   * TODO: Refactor memory allocation strategy!
   */
  size_t result_size = 0;
  SAFE_ALLOC_SIZE(byte, result_data, sizeof (byte) * property->length * OODLE_COMPRESSION_FACTOR_MAGIC);

  UpkOodle upk;
  uint32_t pos = 0;
  do {
    memset(&upk, 0, sizeof (upk));
    memcpy(&upk, (byte *) data->value + pos, sizeof (upk));

    if (memcmp(&upk.signature, signature, signature_len) != 0) {
      printf_error("Compressed block at position %lu and signature %08X does not match %08X",
        pos, _byteswap_ulong(upk.signature), _byteswap_ulong(OODLE_COMPRESSED_BLOCK_SIGNATURE)
      );
      exit(EXIT_FAILURE);
    }

    printf_verbose(verbose, "Compressed Block #%lu:", pos);
    printf_verbose(verbose, " Signature: 0x%08X",_byteswap_ulong(upk.signature));
    printf_verbose(verbose, " Scratch max size: %llu bytes", upk.max_block_size);
    printf_verbose(verbose, " Compressed size: %llu bytes", upk.blocks[0].compressed_size);
    printf_verbose(verbose, " Uncompressed size: %llu bytes", upk.blocks[0].uncompressed_size);

    pos += sizeof (upk);

    uint64_t compressed_size = upk.blocks[0].compressed_size;
    SAFE_ALLOC_SIZE(byte, buffer, sizeof (byte) * compressed_size);
    memcpy(buffer, (byte *) data->value + pos, compressed_size);
    pos += compressed_size;

    uint64_t uncompressed_size = upk.blocks[0].uncompressed_size;
    SAFE_ALLOC_SIZE(byte, output, sizeof (byte) * uncompressed_size);

    int decompressed_bytes = OodleLZ_Decompress(
      buffer, compressed_size,
      output, uncompressed_size,
      OodleLZ_FuzzSafe_No, OodleLZ_CheckCRC_No, OodleLZ_Verbosity_None,
      NULL, 0, NULL, NULL, NULL, 0,
      OodleLZ_Decode_ThreadPhaseAll
    );

    printf_verbose(verbose, " Decompressed: %d bytes", decompressed_bytes);
    if (decompressed_bytes != uncompressed_size) {
      printf_error("Compressed block partial decompression detected! expected %llu bytes; decompressed %lu bytes", uncompressed_size, decompressed_bytes);
      exit(EXIT_FAILURE);
    }

    memcpy(result_data + result_size, output, decompressed_bytes);
    result_size += decompressed_bytes;

    free(buffer);
    free(output);
  } while (pos < data->size);

  /**
   * NOTE: Decompressed sqlite file has a header that specifies the size
   * of the sqlite data (aligned on pages)
   * We want to skip the header, parse sqlite actual header,
   * calculate the sqlite actual data size and skip filler bytes.
   */
  byte *tmp_result_data = result_data;

  UpkOodleSqliteSize upk_sqlite_size;
  COPY_MEMORY(result_data, &upk_sqlite_size, sizeof (upk_sqlite_size));
  printf_verbose(verbose, "Verifying SQLite database integrity...");
  printf_verbose(verbose, " UPK container size: %d", upk_sqlite_size.container_size);
  printf_verbose(verbose, " UPK SQLite size: %d", upk_sqlite_size.sqlite_size);

  SqliteHeader sqlite_header;
  COPY_MEMORY(result_data, &sqlite_header, sizeof (sqlite_header));
  uint16_t sqlite_page_size = _byteswap_ushort(sqlite_header.page_size);
  uint32_t sqlite_database_size = _byteswap_ulong(sqlite_header.database_size);
  printf_verbose(verbose, " SQLite signature \"%s\", page size: %u and database size: %lu", sqlite_header.magic, sqlite_page_size, sqlite_database_size);

  uint32_t sqlite_size = sqlite_page_size * sqlite_database_size;
  printf_verbose(verbose, " SQLite calculate size: %lu", sqlite_size);

  if (sqlite_size != upk_sqlite_size.sqlite_size) {
    printf_error("Expected sqlite database size (%lu) does not match actual size (%lu)", upk_sqlite_size.sqlite_size, sqlite_size);
    exit(EXIT_FAILURE);
  }

  result_data = tmp_result_data;

  SAFE_ALLOC_SIZE(byte, sqlite_data, sqlite_size);
  memcpy(sqlite_data, result_data + sizeof (upk_sqlite_size), sqlite_size);

  /**
   * NOTE: UArrayProperty has + `UPROPERTY_ADDED_LENGTH`
   * bytes added to its length (pointer to embedded TYPE property??)
   */
  free(result_data);
  free(data->value);
  data->size = sqlite_size;
  data->value = sqlite_data;
  property->length = data->size + UARRAYPROPERTY_ADDED_LENGTH;

  releaseOodleLibrary();
}
