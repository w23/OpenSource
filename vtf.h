#pragma once
#include <stdint.h>

#pragma pack(1)
struct VTFHeader {
	char signature[4];
	uint32_t version[2];
	uint32_t header_size;
	uint16_t width, height;
	uint32_t flags;
	uint16_t frames;
	uint16_t first_frame;
	char padding0[4];
	float reflectivity[3];
	char padding1[4];
	float bump_scale;
	uint32_t hires_format;
	uint8_t mipmap_count;
	uint32_t lores_format;
	uint8_t lores_width, lores_height;
};

enum VTFImageFormat {
	VTFImage_None = -1,
	VTFImage_RGBA8 = 0,
	VTFImage_ABGR8,
	VTFImage_RGB8,
	VTFImage_BGR8,
	VTFImage_RGB565,
	VTFImage_I8,
	VTFImage_IA8,
	VTFImage_P8,
	VTFImage_A8,
	VTFImage_RGB8_Bluescreen,
	VTFImage_BGR8_Bluescreen,
	VTFImage_ARGB8,
	VTFImage_BGRA8,
	VTFImage_DXT1,
	VTFImage_DXT3,
	VTFImage_DXT5,
	VTFImage_BGRX8,
	VTFImage_BGR565,
	VTFImage_BGRX5551,
	VTFImage_BGRA4,
	VTFImage_DXT1_A1,
	VTFImage_BGRA5551,
	VTFImage_UV8,
	VTFImage_UVWQ8,
	VTFImage_RGBA16F,
	VTFImage_RGBA16,
	VTFImage_UVLX8
};
#pragma pack()
