#pragma once
#include <stdint.h>

#pragma pack(1)

#define ZipEndOfDirectory_Signature 0x06054b50ul
#define ZipFileHeader_Signature 0x02014b50ul
#define ZipLocalFileHeader_Signature 0x04034b50ul

struct ZipEndOfDirectory {
	uint32_t signature;
	uint16_t disk;
	uint16_t dir_disk;
	uint16_t dir_records_num;
	uint16_t dir_records_num_total;
	uint32_t dir_size;
	uint32_t dir_offset;
	uint16_t comment_size;
};

struct ZipFileHeader {
	uint32_t signature;
	uint16_t version;
	uint16_t extract_version;
	uint16_t flags;
	uint16_t compression;
	uint16_t mod_time, mod_date;
	uint32_t crc32;
	uint32_t compressed_size;
	uint32_t uncompressed_size;
	uint16_t filename_length;
	uint16_t extra_field_length;
	uint16_t file_comment_length;
	uint16_t disk;
	uint16_t file_attribs;
	uint32_t ext_file_attribs;
	uint32_t local_offset;
	/*
	 * - [filename_length] file name
	 * - [extra_field_length] extra field
	 * - [file_comment_length] file comments
	 */
};

struct ZipLocalFileHeader {
	uint32_t signature;
	uint16_t extract_version;
	uint16_t flags;
	uint16_t compression;
	uint16_t mod_time;
	uint16_t mod_date;
	uint32_t crc32;
	uint32_t compressed_size, uncompressed_size;
	uint16_t filename_length, extra_field_length;
	/* - [filename_length] file name
	 * - [extra_field_length] extra field
	 */
};

#pragma pack()
