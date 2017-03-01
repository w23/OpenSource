#ifndef ATLAS_H__INCLUDED
#define ATLAS_H__INCLUDED

enum AtlasResult {
	Atlas_Success = 0,
	Atlas_ErrorDoesntFit,
	Atlas_ErrorInsufficientTemp
};

struct AtlasVec { unsigned int x, y; };

struct AtlasContext {
	/* temporary buffer/scrap space for atlas to use */
	/* worst case consumption: 4 * sizeof(unsigned int) * (1 + rects_count) */
	struct {
		void *ptr;
		unsigned int size;
	} temp_storage;
	
	/* input */
	unsigned int width, height;
	const struct AtlasVec *rects;
	unsigned int rects_count;
	unsigned int rects_stride;

	/* output */
	struct AtlasVec *pos;
	unsigned int pos_stride;
};

/* TODO ?
struct AtlasStats {
	unsigned int wasted_pixels;
	struct AtlasVec min_waste, max_waste;
	unsigned int exact_matches;
};
*/

enum AtlasResult atlasCompute(const struct AtlasContext* context);

#endif /* ATLAS_H__INCLUDED */
