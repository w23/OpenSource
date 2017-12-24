#include "etcpack.h"
#include "libc.h"

static const int etc1_mod_table[8][4] = {
	{2, 8, -2, -8},
	{5, 17, -5, -17},
	{9, 29, -9, -29},
	{13, 42, -13, -42},
	{18, 60, -18, -60},
	{24, 80, -24, -80},
	{33, 106, -33, -106},
	{47, 183, -47, -183},
};

typedef struct {
	ETC1Color base;
	int table;
	int error;
	uint8_t msb, lsb;
} ETC1SubblockPacked;

static int clamp8(int i) { return (i < 0) ? 0 : (i > 255) ? 255 : i; }

#define ETC1_PIXEL_ERROR_MAX 1000
static int etc1PixelError(ETC1Color a, ETC1Color b) {
	return abs(a.r - b.r) + abs(a.g - b.g) + abs(a.b - b.b);
}

static ETC1SubblockPacked etc1PackSubblock2x4(const ETC1Color *in4x2) {
	ETC1Color average = {.r = 0, .g = 0, .b = 0};

	for (int i = 0; i < 8; ++i) {
		average.r += in4x2[i].r;
		average.g += in4x2[i].g;
		average.b += in4x2[i].b;
	}

	average.r >>= 3;
	average.g >>= 3;
	average.b >>= 3;

	ETC1SubblockPacked packed = {
		.error = ETC1_PIXEL_ERROR_MAX * 8,
	};
	for (int itbl = 0; itbl < 8; ++itbl) {
		const int *const pmod = etc1_mod_table[itbl];
		ETC1SubblockPacked variant = {
			.base = average,
			.table = itbl,
			.error = 0,
			.msb = 0, .lsb = 0,
		};
		for (int ip = 0; ip < 8; ++ip) {
			const ETC1Color c = in4x2[ip];
			int best_pixel_error = ETC1_PIXEL_ERROR_MAX;
			int best_pixel_imod = 0;
			for (int im = 0; im < 4; ++im) {
				const int mod = pmod[im];
				const ETC1Color mc = {
					.r = clamp8(variant.base.r + mod),
					.g = clamp8(variant.base.g + mod),
					.b = clamp8(variant.base.b + mod)
				};
				const int perr = etc1PixelError(c, mc);

				if (perr < best_pixel_error) {
					best_pixel_error = perr;
					best_pixel_imod = im;
				}
			}

			variant.lsb >>= 1;
			variant.msb >>= 1;
			variant.lsb |= (best_pixel_imod & 1) << 7; 
			variant.msb |= (best_pixel_imod & 2) << 7; 

			variant.error += best_pixel_error;
		}

		if (variant.error < packed.error)
			packed = variant;
	}
	return packed;
}

void etc1PackBlock(const ETC1Color *in4x4, uint8_t *out) {
	const ETC1SubblockPacked sub1 = etc1PackSubblock2x4(in4x4);
	const ETC1SubblockPacked sub2 = etc1PackSubblock2x4(in4x4 + 8);

	out[0] = (sub1.base.r & 0xf0) | (sub2.base.r >> 4);
	out[1] = (sub1.base.g & 0xf0) | (sub2.base.g >> 4);
	out[2] = (sub1.base.b & 0xf0) | (sub2.base.b >> 4);
	out[3] = (sub1.table << 5) | (sub2.table << 2) | 0x00; // diffbit = 0, flipbit = 0
	out[4] = sub2.msb;
	out[5] = sub1.msb;
	out[6] = sub2.lsb;
	out[7] = sub1.lsb;
}
