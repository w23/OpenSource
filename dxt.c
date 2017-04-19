#include "dxt.h"
#include "libc.h"
#include <stdint.h>

static uint16_t dxtColorSum(int m1, uint16_t c1, int m2, uint16_t c2, int add, int denom) {
	const int mask_rb = 0xf81f, mask_g = 0x07e0;
	const uint32_t rb1 = c1 & mask_rb, g1 = c1 & mask_g,
		rb2 = c2 & mask_rb, g2 = c2 & mask_g;
	add |= (add << 5) | (add << 11);
	return (((m1 * rb1 + m2 * rb2 + add) / denom) & mask_rb)
		| (((m1 * g1 + m2 * g2 + add) / denom) & mask_g);
}

void dxt1Unpack(struct DXTUnpackContext ctx) {
	if (ctx.width < 4 || ctx.height < 4 || ctx.width & 3 || ctx.height & 3)
		return;

	const uint16_t transparent = 0;
	const uint8_t *src = (const uint8_t*)ctx.packed;
	for (int y = 0; y < ctx.height; y+=4) {
		uint16_t *dst_4x4 = (uint16_t*)ctx.output + ctx.width * y;
		for (int x = 0; x < ctx.width; x+=4, dst_4x4 += 4, src += 8) {
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
				pix[0] = c[(bitmap >> 6) & 3];
				pix[1] = c[(bitmap >> 4) & 3];
				pix[2] = c[(bitmap >> 2) & 3];
				pix[3] = c[(bitmap >> 0) & 3];
			} /* for all rows in 4x4 */
		} /* for x */
	} /* for y */
}

void dxt5Unpack(struct DXTUnpackContext ctx) {
	(void)ctx;
	/* FIXME */
	dxt1Unpack(ctx);
}
