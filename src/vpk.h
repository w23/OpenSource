#pragma once
#include <stdint.h>

#pragma pack(1)
#define VPK_SIGNATURE (0x55aa1234ul)
typedef struct {
	uint32_t signature;
	uint32_t version;
	uint32_t treeSize;
} VPK1Header;
typedef struct {
	uint32_t signature;
	uint32_t version;
	uint32_t treeSize;
	uint32_t dontCareSize[4];
} VPK2Header;

#define VPK_TERMINATOR (0xffffu)
struct VPKTreeEntry {
	uint32_t crc;
	uint16_t preloadBytes;
	uint16_t archive;
	uint32_t archiveOffset;
	uint32_t archiveLength;
	uint16_t terminator;
};

#pragma pack()
