#include "texture.h"
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
			pixel_bits = 8;
			break;
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

static int textureLoad(struct IFile *file, Texture *tex, struct Stack *tmp) {
	struct VTFHeader hdr;
	size_t cursor = sizeof hdr;
	if (file->read(file, 0, sizeof(hdr), &hdr) != sizeof(hdr)) {
		PRINT("Cannot read texture");
		return 0;
	}

	if (hdr.signature[0] != 'V' || hdr.signature[1] != 'T' ||
			hdr.signature[2] != 'F' || hdr.signature[3] != '\0') {
		PRINT("Invalid file signature");
		return 0;
	}

	PRINTF("Texture: %dx%d, %s",
		hdr.width, hdr.height, vtfFormatStr(hdr.hires_format));

	cursor += vtfImageSize(hdr.lores_format, hdr.lores_width, hdr.lores_height);

	/* TODO don't skip all mipmaps */
	for (int mip = hdr.mipmap_count; mip > 0; --mip)
		for (int frame = hdr.first_frame; frame < hdr.frames; ++frame)
			for (int face = 0; face < 1; ++face)
				for (int z = 0; z < 1; ++z)
					cursor += vtfImageSize(hdr.hires_format, hdr.width >> mip, hdr.height >> mip);

	int retval = 0;
	void *pre_alloc_cursor = stackGetCursor(tmp);
	const int src_texture_size = vtfImageSize(hdr.hires_format, hdr.width, hdr.height);
	void *src_texture = stackAlloc(tmp, src_texture_size);
	if (!src_texture) {
		PRINTF("Cannot allocate %d bytes for texture", src_texture_size);
		goto exit;
	}
	const int dst_texture_size = 2 * hdr.width * hdr.height;
	void *dst_texture = stackAlloc(tmp, dst_texture_size);
	if (!dst_texture) {
		PRINTF("Cannot allocate %d bytes for texture", dst_texture_size);
		goto exit;
	}
	if (src_texture_size != (int)file->read(file, 0, src_texture_size, src_texture)) {
		PRINT("Cannot read texture data");
		goto exit;
	}

	const struct DXTUnpackContext dxt_ctx = {
		.width = hdr.width,
		.height = hdr.height,
		.packed = src_texture,
		.output = dst_texture
	};
	dxt1Unpack(dxt_ctx);

	const AGLTextureUploadData data = {
		.x = 0,
		.y = 0,
		.width = hdr.width,
		.height = hdr.height,
		.format = AGLTF_U565_RGB,
		.pixels = dst_texture
	};

	tex->gltex = aGLTextureCreate();
	aGLTextureUpload(&tex->gltex, &data);
	retval = 1;

exit:
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

	struct Texture localtex;
	if (textureLoad(texfile, &localtex, tmp) == 0) {
		PRINTF("Texture \"%s\" found, but could not be loaded", name);
	} else {
		tex = &localtex;
		cachePutTexture(name, tex);
	}

	texfile->close(texfile);
	return tex ? tex : cacheGetTexture("opensource/placeholder");
}
