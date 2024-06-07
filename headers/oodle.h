#pragma once

#pragma pack(push, 1)
/**
 * Structure is as follows:
 *
 * [C1 83 2A 9E 00 00 00 00] signature
 * [00 00 02 00 00 00 00 00] max block size
 * [
 *  [73 30 00 00 00 00 00 00] compressed size
 *  [00 00 02 00 00 00 00 00] uncompressed size
 *  [73 30 00 00 00 00 00 00] compressed size
 *  [00 00 02 00 00 00 00 00] uncompressed size
 * ]
 * [8C 06] oodle kraken header
 *
 * Why compressed and uncompressed size is repeated...
 * I don't know (haven't dig into it).
 */
typedef struct _UPK_BLOCK {
  uint64_t compressed_size;
  uint64_t uncompressed_size;
} UpkBlock;

typedef struct _UPK_OODLE {
  uint64_t signature;
  uint64_t max_block_size;
  UpkBlock blocks[2];
} UpkOodle;

/**
 * Uncompressed SQLite database has a header
 * Structure is as follows:
 * [04 20 1F 00] database size + 4 bytes
 * [00 20 1F 00] actual database size
 * [53 51 4C 69 ...] SQLite header...
 *
 * We need to skip the header when uncompressing,
 * so existing save file editors can work.
 */
typedef struct _UPK_OODLE_SQLITE_SIZE {
  uint32_t container_size;
  uint32_t sqlite_size;
} UpkOodleSqliteSize;

/**
 * Partial SQLite header structure
 * used for calculating the actual database size
 * for validation purposes and for writing
 * the header when compressing the database
 */
typedef struct _SQLITE_HEADER {
  char magic[16];
  uint16_t page_size;
  uint8_t unused[10];
  uint32_t database_size;
} SqliteHeader;
#pragma pack(pop)

/**
 * Some Oodle structures are partially
 * defined!
 */
typedef enum OodleLZ_Compressor {
  OodleLZ_Compressor_Invalid = -1,
  OodleLZ_Compressor_Kraken = 8
} OodleLZ_Compressor;

typedef enum OodleLZ_CompressionLevel {
  OodleLZ_CompressionLevel_Fast = 3
} OodleLZ_CompressionLevel;

typedef enum OodleLZ_FuzzSafe {
  OodleLZ_FuzzSafe_No = 0,
  OodleLZ_FuzzSafe_Yes = 1
} OodleLZ_FuzzSafe;

typedef enum OodleLZ_CheckCRC {
  OodleLZ_CheckCRC_No = 0,
  OodleLZ_CheckCRC_Yes = 1,
  OodleLZ_CheckCRC_Force32 = 0x40000000
} OodleLZ_CheckCRC;

typedef enum OodleLZ_Verbosity {
  OodleLZ_Verbosity_None = 0
} OodleLZ_Verbosity;

typedef enum OodleLZ_Decode_ThreadPhase {
  OodleLZ_Decode_ThreadPhaseAll = 3
} OodleLZ_Decode_ThreadPhase;

typedef enum OodleLZ_Profile {
  OodleLZ_Profile_Main = 0
} OodleLZ_Profile;

typedef enum OodleLZ_Jobify {
  OodleLZ_Jobify_Default = 0,
  OodleLZ_Jobify_Disable = 1,
  OodleLZ_Jobify_Normal = 2,
  OodleLZ_Jobify_Aggressive = 3,
  OodleLZ_Jobify_Count = 4,
  OodleLZ_Jobify_Force32 = 0x40000000,
} OodleLZ_Jobify;

typedef struct OodleLZ_CompressOptions {
  uint32_t unused_was_verbosity;
  int32_t minMatchLen;
  int32_t seekChunkReset;
  int32_t seekChunkLen;
  OodleLZ_Profile profile;
  int32_t dictionarySize;
  int32_t spaceSpeedTradeoffBytes;
  int32_t unused_was_maxHuffmansPerChunk;
  int32_t sendQuantumCRCs;
  int32_t maxLocalDictionarySize;
  int32_t makeLongRangeMatcher;
  int32_t matchTableSizeLog2;
  OodleLZ_Jobify jobify;
  void *jobifyUserPtr;
  int32_t farMatchMinLen;
  int32_t farMatchOffsetLog2;
  uint32_t reserved[4];
} OodleLZ_CompressOptions;

/**
 * OodleLZ function prototypes
 */
typedef OodleLZ_CompressOptions * OodleLZ_CompressOptions_GetDefault_FP(
  OodleLZ_Compressor compressor, OodleLZ_CompressionLevel lzLevel
);

typedef intptr_t OodleLZ_GetCompressedBufferSizeNeeded_FP (
  OodleLZ_Compressor compressor, uintptr_t rawSize
);

typedef int WINAPI OodleLZ_Compress_FP (
  OodleLZ_Compressor compressor, uint8_t *data_buf, size_t data_len, uint8_t *dst_buff,
  OodleLZ_CompressionLevel compression, OodleLZ_CompressOptions *cmps_opts,
  const void *dictionary, const void *lrmv, void *scratch, size_t scratch_size
);

typedef int WINAPI OodleLZ_Decompress_FP (
  uint8_t *src_buf, size_t src_len, uint8_t *dst_buff, size_t dst_size, int fuzz,
  int crc, int verbose, uint8_t *dst_base, size_t e, void *cb, void *cb_ctx,
  void *scratch, size_t scratch_size, int threadPhase
);

// public
void compress(UProperty *property, bool verbose);
void decompress(UProperty *property, bool verbose);
