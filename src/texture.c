#include "texture.h"
#include "etcpack.h"
#include "dxt.h"
#include "vtf.h"
#include "cache.h"
#include "collection.h"
#include "mempools.h"
#include "common.h"

const char *vtfFormatStr(enum VTFImageFormat fmt) {
	switch(fmt) {
		case VTFImage_None: return "None";
		case VTFImage_RGBA8: return "RGBA8";
		case VTFImage_ABGR8: return "ABGR8";
		case VTFImage_RGB8: return "RGB8";
		case VTFImage_BGR8: return "BGR8";
		case VTFImage_RGB565: return "RGB565";
		case VTFImage_I8: return "I8";
		case VTFImage_IA8: return "IA8";
		case VTFImage_P8: return "P8";
		case VTFImage_A8: return "A8";
		case VTFImage_RGB8_Bluescreen: return "RGB8_Bluescreen";
		case VTFImage_BGR8_Bluescreen: return "BGR8_Bluescreen";
		case VTFImage_ARGB8: return "ARGB8";
		case VTFImage_BGRA8: return "BGRA8";
		case VTFImage_DXT1: return "DXT1";
		case VTFImage_DXT3: return "DXT3";
		case VTFImage_DXT5: return "DXT5";
		case VTFImage_BGRX8: return "BGRX8";
		case VTFImage_BGR565: return "BGR565";
		case VTFImage_BGRX5551: return "BGRX5551";
		case VTFImage_BGRA4: return "BGRA4";
		case VTFImage_DXT1_A1: return "DXT1_A1";
		case VTFImage_BGRA5551: return "BGRA5551";
		case VTFImage_UV8: return "UV8";
		case VTFImage_UVWQ8: return "UVWQ8";
		case VTFImage_RGBA16F: return "RGBA16F";
		case VTFImage_RGBA16: return "RGBA16";
		case VTFImage_UVLX8: return "UVLX8";
	}
	return "None";
}

static int vtfImageSize(enum VTFImageFormat fmt, int width, int height) {
	int pixel_bits = 0;
	switch(fmt) {
		case VTFImage_None: break;
		case VTFImage_RGBA8:
		case VTFImage_ABGR8:
		case VTFImage_ARGB8:
		case VTFImage_BGRA8:
		case VTFImage_BGRX8:
		case VTFImage_UVWQ8:
		case VTFImage_UVLX8:
			pixel_bits = 32;
			break;
		case VTFImage_BGR565:
		case VTFImage_BGRX5551:
		case VTFImage_BGRA5551:
		case VTFImage_BGRA4:
		case VTFImage_RGB565:
		case VTFImage_UV8:
			pixel_bits = 16;
			break;
		case VTFImage_RGB8:
		case VTFImage_BGR8:
		case VTFImage_RGB8_Bluescreen:
		case VTFImage_BGR8_Bluescreen:
			pixel_bits = 24;
			break;
		case VTFImage_I8:
		case VTFImage_IA8:
		case VTFImage_P8:
		case VTFImage_A8:
			pixel_bits = 8;
			break;
		case VTFImage_DXT3:
		case VTFImage_DXT5:
			pixel_bits = 4;
			/* fall through */
		case VTFImage_DXT1:
		case VTFImage_DXT1_A1:
			pixel_bits += 4;
			width = 4 * ((width + 3) / 4);
			height = 4 * ((height + 3) / 4);
			break;
		case VTFImage_RGBA16F:
		case VTFImage_RGBA16:
			pixel_bits = 64;
			break;
	}
	return width * height * pixel_bits / 8;
}

static void textureUnpackDXTto565(uint8_t *src, uint16_t *dst, int width, int height, enum VTFImageFormat format) {
	const struct DXTUnpackContext dxt_ctx = {
		.width = width,
		.height = height,
		.packed = src,
		.output = dst
	};

	if (format == VTFImage_DXT1)
		dxt1Unpack(dxt_ctx);
	else
		dxt5Unpack(dxt_ctx);
}

static void textureUnpackBGR8to565(uint8_t *src, uint16_t *dst, int width, int height) {
	const int pixels = width * height;
	for (int i = 0; i < pixels; ++i, src+=3) {
		const int r = (src[0] >> 3);
		const int g = (src[1] >> 2);
		const int b = (src[2] >> 3);
		dst[i] = (uint16_t)((r << 11) | (g << 5) | b);
	}
}

static void textureUnpackBGRX8to565(uint8_t *src, uint16_t *dst, int width, int height) {
	const int pixels = width * height;
	for (int i = 0; i < pixels; ++i, src+=4) {
		const int r = (src[0] & 0xf8);
		const int g = (src[1] & 0xfc);
		const int b = (src[2] >> 3);
		dst[i] = (uint16_t)((r << 8) | (g << 3)| b);
	}
}

static void textureUnpackBGRA8to565(uint8_t *src, uint16_t *dst, int width, int height) {
	const int pixels = width * height;
	for (int i = 0; i < pixels; ++i, src+=4) {
		const int a = src[3] * 8; /* FIXME this is likely HDR and need proper color correction */
		const int r = ((a * src[0]) >> 11);
		const int g = ((a * src[1]) >> 10);
		const int b = (a * src[2]) >> 11;

		dst[i] = (uint16_t)(((r>31?31:r) << 11) | ((g>63?63:g) << 5) | (b>31?31:b));
	}
}

/* FIXME: taken from internets: https://gist.github.com/martinkallman/5049614 */
float float32(uint16_t value) {
	const uint32_t result =
		(((value & 0x7fffu) << 13) + 0x38000000u)	|  // mantissa + exponent
		((value & 0x8000u) << 16); // sign
	float retval;
	memcpy(&retval, &result, sizeof(retval));
	return retval;
}

static void textureUnpackRGBA16Fto565(const uint16_t *src, uint16_t *dst, int width, int height) {
	const int pixels = width * height;
	for (int i = 0; i < pixels; ++i, src+=4) {
		const float scale = 255.f * 1.5f;
		const int rf = (int)(sqrtf(float32(src[0])) * scale) >> 3;
		const int gf = (int)(sqrtf(float32(src[1])) * scale) >> 2;
		const int bf = (int)(sqrtf(float32(src[2])) * scale) >> 3;
		dst[i] = (uint16_t)(((rf>31?31:rf) << 11) | ((gf>63?63:gf) << 5) | (bf>31?31:bf));
	}
}

static uint16_t *textureUnpackToTemp(struct Stack *tmp, struct IFile *file, size_t cursor,
		int width, int height, enum VTFImageFormat format) {

	const int src_texture_size = vtfImageSize(format, width, height);
	const int dst_texture_size = sizeof(uint16_t) * width * height;
	void *dst_texture = stackAlloc(tmp, dst_texture_size);
	if (!dst_texture) {
		PRINTF("Cannot allocate %d bytes for texture", dst_texture_size);
		return 0;
	}
	void *src_texture = stackAlloc(tmp, src_texture_size);
	if (!src_texture) {
		PRINTF("Cannot allocate %d bytes for texture", src_texture_size);
		return 0;
	}

	if (src_texture_size != (int)file->read(file, cursor, src_texture_size, src_texture)) {
		PRINT("Cannot read texture data");
		return 0;
	}

	switch (format) {
		case VTFImage_DXT1:
		case VTFImage_DXT5:
			textureUnpackDXTto565(src_texture, dst_texture, width, height, format);
			break;
		case VTFImage_BGR8:
			textureUnpackBGR8to565(src_texture, dst_texture, width, height);
			break;
		case VTFImage_BGRA8:
			textureUnpackBGRA8to565(src_texture, dst_texture, width, height);
			break;
		case VTFImage_BGRX8:
			textureUnpackBGRX8to565(src_texture, dst_texture, width, height);
			break;
		case VTFImage_RGBA16F:
			textureUnpackRGBA16Fto565(src_texture, dst_texture, width, height);
			break;
		default:
			PRINTF("Unsupported texture format %s", vtfFormatStr(format));
			return 0;
	}

	stackFreeUpToPosition(tmp, src_texture);
	return dst_texture;
}
/*  */
/* static int textureUploadMipmapType(struct Stack *tmp, struct IFile *file, size_t cursor, */
/* 		const struct VTFHeader *hdr, int miplevel, RTexture *tex, RTexType tex_type) { */
/* 	for (int mip = hdr->mipmap_count - 1; mip > miplevel; --mip) { */
/* 		const unsigned int mip_width = hdr->width >> mip; */
/* 		const unsigned int mip_height = hdr->height >> mip; */
/* 		const int mip_image_size = vtfImageSize(hdr->hires_format, mip_width, mip_height); */
/* 		cursor += mip_image_size * hdr->frames; */
/*  */
/* 		#<{(|PRINTF("cur: %d; size: %d, mip: %d, %dx%d", */
/* 				cursor, mip_image_size, mip, mip_width, mip_height); */
/* 		|)}># */
/* 	} */
/*  */
/* 	#<{(| if (hdr->hires_format == VTFImage_DXT1 |)}># */
/* 	#<{(| 		|| hdr->hires_format == VTFImage_DXT1_A1 |)}># */
/* 	#<{(| 		|| hdr->hires_format == VTFImage_DXT3 |)}># */
/* 	#<{(| 		|| hdr->hires_format == VTFImage_DXT5) |)}># */
/* 	{ */
/* 		const int src_texture_size = vtfImageSize(hdr->hires_format, hdr->width, hdr->height); */
/* 		void *src_texture = stackAlloc(tmp, src_texture_size); */
/* 		if (!src_texture) { */
/* 			PRINTF("Cannot allocate %d bytes for texture", src_texture_size); */
/* 			return 0; */
/* 		} */
/*  */
/* 		if (src_texture_size != (int)file->read(file, cursor, src_texture_size, src_texture)) { */
/* 			PRINT("Cannot read texture data"); */
/* 			return 0; */
/* 		} */
/*  */
/* 		RTexFormat fmt; */
/* 		switch (hdr->hires_format) { */
/* 			case VTFImage_DXT1: fmt = RTexFormat_Compressed_DXT1; break; */
/* 			case VTFImage_DXT1_A1: fmt = RTexFormat_Compressed_DXT1_A1; break; */
/* 			case VTFImage_DXT3: fmt = RTexFormat_Compressed_DXT3; break; */
/* 			case VTFImage_DXT5: fmt = RTexFormat_Compressed_DXT5; break; */
/* 			default: return 0; */
/* 		} */
/*  */
/* 		const RTextureUploadParams params = { */
/* 			.type = tex_type, */
/* 			.width = hdr->width, */
/* 			.height = hdr->height, */
/* 			.format = fmt, */
/* 			.pixels = src_texture, */
/* 			.mip_level = -2,//miplevel, */
/* 			.wrap =  RTexWrap_Repeat */
/* 		}; */
/*  */
/* 		renderTextureUpload(tex, params); */
/* 		return 1; */
/* 	} */
/*  */
/* 	void *dst_texture = textureUnpackToTemp(tmp, file, cursor, hdr->width, hdr->height, hdr->hires_format); */
/* 	if (!dst_texture) { */
/* 		PRINT("Failed to unpack texture"); */
/* 		return 0; */
/* 	} */
/*  */
/* #ifdef ATTO_PLATFORM_RPI */
/* 	{ */
/* 		const uint16_t *p565 = dst_texture; */
/* 		uint8_t *etc1_data = stackAlloc(tmp, hdr->width * hdr->height / 2); */
/* 		uint8_t *block = etc1_data; */
/*  */
/* 		// FIXME assumes w and h % 4 == 0 */
/* 		for (int by = 0; by < hdr->height; by += 4) { */
/* 			for (int bx = 0; bx < hdr->width; bx += 4) { */
/* 				const uint16_t *bp = p565 + bx + by * hdr->width; */
/* 				ETC1Color ec[16]; */
/* 				for (int x = 0; x < 4; ++x) { */
/* 					for (int y = 0; y < 4; ++y) { */
/* 						const unsigned p = bp[x + y * hdr->width]; */
/* 						ec[x*4+y].r = (p & 0xf800u) >> 8; */
/* 						ec[x*4+y].g = (p & 0x07e0u) >> 3; */
/* 						ec[x*4+y].b = (p & 0x001fu) << 3; */
/* 					} */
/* 				} */
/* 				 */
/* 				etc1PackBlock(ec, block); */
/* 				block += 8; */
/* 			} */
/* 		} */
/*  */
/* 		const RTextureUploadParams params = { */
/* 			.type = tex_type, */
/* 			.width = hdr->width, */
/* 			.height = hdr->height, */
/* 			.format = RTexFormat_Compressed_ETC1, */
/* 			.pixels = etc1_data, */
/* 			.mip_level = -2,//miplevel, */
/* 			.wrap =  RTexWrap_Repeat */
/* 		}; */
/*  */
/* 		renderTextureUpload(tex, params); */
/*  */
/* 		stackFreeUpToPosition(tmp, etc1_data); */
/* 	} */
/* #else */
/*  */
/* 	const RTextureUploadParams params = { */
/* 		.type = tex_type, */
/* 		.width = hdr->width, */
/* 		.height = hdr->height, */
/* 		.format = RTexFormat_RGB565, */
/* 		.pixels = dst_texture, */
/* 		.mip_level = -1,//miplevel, */
/* 		.wrap =  RTexWrap_Repeat */
/* 	}; */
/*  */
/* 	renderTextureUpload(tex, params); */
/* #endif */
/*  */
/* 	return 1; */
/* } */

static int textureLoad(struct IFile *file, Texture *tex, struct Stack *tmp, RTexType type) {
	struct VTFHeader hdr;
	size_t cursor = 0;
	int retval = 0;
	if (file->read(file, 0, sizeof(hdr), &hdr) != sizeof(hdr)) {
		PRINT("Cannot read texture");
		return 0;
	}

	if (hdr.signature[0] != 'V' || hdr.signature[1] != 'T' ||
			hdr.signature[2] != 'F' || hdr.signature[3] != '\0') {
		PRINT("Invalid file signature");
		return 0;
	}

#define MAX_MIPS 12
	if (hdr.mipmap_count > MAX_MIPS) {
		PRINTF("Too many mips: %d", (int)hdr.mipmap_count);
		return 0;
	}

	RTexFormat format;
	switch (hdr.hires_format) {
		case VTFImage_DXT1: format = RTexFormat_Compressed_DXT1; break;
		case VTFImage_DXT1_A1: format = RTexFormat_Compressed_DXT1_A1; break;
		case VTFImage_DXT3: format = RTexFormat_Compressed_DXT3; break;
		case VTFImage_DXT5: format = RTexFormat_Compressed_DXT5; break;
		default: return 0;
	}

	cursor += hdr.header_size;

	/* Compute averaga color from lowres image */
	void *pre_alloc_cursor = stackGetCursor(tmp);
	if (hdr.lores_format != VTFImage_DXT1 && hdr.lores_format != VTFImage_DXT5) {
		PRINTF("Not implemented lores texture format: %s", vtfFormatStr(hdr.lores_format));
		tex->avg_color = aVec3ff(1.f);
	} else {
		uint16_t *pixels = textureUnpackToTemp(tmp, file, cursor, hdr.lores_width, hdr.lores_height, hdr.lores_format);

		// FIXME clean temp memory properly
		if (!pixels) {
			PRINT("Cannot unpack lowres image");
			return 0;
		}

		tex->avg_color = aVec3ff(0);
		const int pixels_count = hdr.lores_width * hdr.lores_height;
		for (int i = 0; i < pixels_count; ++i) {
			tex->avg_color.x += (pixels[i] >> 11);
			tex->avg_color.y += (pixels[i] >> 5) & 0x3f;
			tex->avg_color.z += (pixels[i] & 0x1f);
		}

		tex->avg_color = aVec3fMul(tex->avg_color,
				aVec3fMulf(aVec3f(1.f/31.f, 1.f/63.f, 1.f/31.f), 1.f / pixels_count));
		//PRINTF("Average color %f %f %f", tex->avg_color.x, tex->avg_color.y, tex->avg_color.z);
	}

	cursor += vtfImageSize(hdr.lores_format, hdr.lores_width, hdr.lores_height);

	// 1. Compute memory size for all mips
	RTextureUploadMipmapData mips[MAX_MIPS] = {0};
	size_t mips_size = 0;
	for (int mip = hdr.mipmap_count - 1; mip >= 0; --mip) {
		const unsigned int mip_width = hdr.width >> mip;
		const unsigned int mip_height = hdr.height >> mip;
		RTextureUploadMipmapData *const m = mips + mip;
		m->width = mip_width;
		m->height = mip_height;
		m->mip_level = mip;
		m->offset = mips_size;
		mips_size += vtfImageSize(hdr.hires_format, mip_width, mip_height);
	}

	// 2. Request render stage buffer memory for all mipmaps
	RStagingMemory mem = renderGetStagingBuffer(mips_size);
	uint8_t * const pixels = mem.ptr;

	// 3. Read texture mipmaps into stage memory, filling mips struct
	for (int i = 0; i < hdr.mipmap_count; ++i) {
		RTextureUploadMipmapData *const m = mips + (hdr.mipmap_count - i - 1);
		const int mip_image_size = vtfImageSize(hdr.hires_format, m->width, m->height);

		if (mip_image_size != (int)file->read(file, cursor, mip_image_size, pixels + m->offset)) {
			PRINT("Cannot read texture data");
			// FIXME free
			return 0;
		}

		cursor += mip_image_size * hdr.frames;
	}

	// 4. Create and upload this data to render as a texutre with mips
	const RTextureUploadParams params = {
		.kind = RTexKind_Material0, // TODO is it?
		.type = type,
		.width = hdr.width,
		.height = hdr.height,
		.format = format,
		.mipmaps_count = hdr.mipmap_count,
		.mipmaps = mips,
		.staging = &mem,
	};

	tex->texture = renderTextureCreateAndUpload(params);
	retval = !!tex->texture.vkImage;

	renderFreeStagingBuffer(mem);
	stackFreeUpToPosition(tmp, pre_alloc_cursor);

	return retval;
}

const Texture *textureGet(const char *name, struct ICollection *collection, struct Stack *tmp) {
	const Texture *tex = cacheGetTexture(name);
	if (tex) return tex;

	struct IFile *texfile;
	if (CollectionOpen_Success != collectionChainOpen(collection, name, File_Texture, &texfile)) {
		PRINTF("Texture \"%s\" not found", name);
		return cacheGetTexture("opensource/placeholder");
	}

	struct Texture localtex = {0};
	if (textureLoad(texfile, &localtex, tmp, RTexType_2D) == 0) {
		PRINTF("Texture \"%s\" found, but could not be loaded", name);
	} else {
		cachePutTexture(name, &localtex);
		tex = cacheGetTexture(name);
	}

	texfile->close(texfile);
	return tex ? tex : cacheGetTexture("opensource/placeholder");
}
