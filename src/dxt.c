#include "dxt.h"
#include "libc.h"
#include <stdint.h>

static uint16_t dxtColorSum(int m1, uint16_t c1, int m2, uint16_t c2, int add, int denom) {
	const int mask_r = 0xf800, shift_r = 11;
	const int mask_g = 0x07e0, shift_g = 5;
	const int mask_b = 0x001f, shift_b = 0;
	const int r =
		(((c1 & mask_r) >> shift_r) * m1 +
		((c2 & mask_r) >> shift_r) * m2 + add) / denom;
	const int g =
		(((c1 & mask_g) >> shift_g) * m1 +
		((c2 & mask_g) >> shift_g) * m2 + add) / denom;
	const int b =
		(((c1 & mask_b) >> shift_b) * m1 +
		((c2 & mask_b) >> shift_b) * m2 + add) / denom;
	return ((r << shift_r) & mask_r) | ((g << shift_g) & mask_g) | ((b << shift_b) & mask_b);
}

void dxtUnpack(struct DXTUnpackContext ctx, int offset) {
	if (ctx.width < 4 || ctx.height < 4 || ctx.width & 3 || ctx.height & 3)
		return;

	const uint16_t transparent = 0;
	const uint8_t *src = (const uint8_t*)ctx.packed;
	for (int y = 0; y < ctx.height; y+=4) {
		uint16_t *dst_4x4 = (uint16_t*)ctx.output + ctx.width * y;
		for (int x = 0; x < ctx.width; x+=4, dst_4x4 += 4, src += offset) {
			uint16_t c[4];
			memcpy(c, src, 2);
			memcpy(c+1, src + 2, 2);

			if (c[0] > c[1]) {
				c[2] = dxtColorSum(2, c[0], 1, c[1], 1, 3);
				c[3] = dxtColorSum(1, c[0], 2, c[1], 1, 3);
			} else {
				c[2] = dxtColorSum(1, c[0], 1, c[1], 0, 2);
				c[3] = transparent;
			}

			uint16_t *pix = dst_4x4;
			for (int r = 0; r < 4; ++r, pix += ctx.width) {
				const uint8_t bitmap = src[4 + r];
				pix[3] = c[(bitmap >> 6) & 3];
				pix[2] = c[(bitmap >> 4) & 3];
				pix[1] = c[(bitmap >> 2) & 3];
				pix[0] = c[(bitmap >> 0) & 3];
			} /* for all rows in 4x4 */
		} /* for x */
	} /* for y */
}

void dxt1Unpack(struct DXTUnpackContext ctx) {
	dxtUnpack(ctx, 8);
}

void dxt5Unpack(struct DXTUnpackContext ctx) {
	ctx.packed = ((char*)ctx.packed) + 8;
	dxtUnpack(ctx, 16);
}
