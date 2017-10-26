#include "atlas.h"

struct AtlasRect { unsigned int x, y, w, h; };

/* temp memory is freed by caller */
enum AtlasResult atlasCompute(const struct AtlasContext* context) {
	const unsigned int max_rects_count = 1 + context->rects_count;
	unsigned int rects_count = 1;
	if (context->temp_storage.size < sizeof(struct AtlasRect) * max_rects_count)
		return Atlas_ErrorInsufficientTemp;
	struct AtlasRect *rects = context->temp_storage.ptr;

	rects[0].x = rects[0].y = 0;
	rects[0].w = context->width; rects[0].h = context->height;

	for (unsigned int i = 0; i < context->rects_count; ++i) {
		const struct AtlasVec * const item = (void*)((char*)context->rects + i * context->rects_stride);
		const unsigned int area = item->x * item->y;

		/* find best fit for this rect */
		struct AtlasRect *fit = 0;
		struct AtlasRect *first_empty = 0; /* optimization */
		unsigned int wasted_area = context->width * context->height;
		for (unsigned int j = 0; j < rects_count; ++j) {
			struct AtlasRect *const r = rects + j;
			if (!first_empty && (!r->w || !r->h)) first_empty = r;

			if (r->w < item->x || r->h < item->y)
				continue;

			/* exact match is the best */
			if (r->w == item->x && r->h == item->y) {
				fit = r;
				break;
			}

			const unsigned int r_area = r->w * r->h;
			const unsigned int fit_waste = r_area - area;
			if (!fit || ((r->w < fit->w || r->h < fit->h) && (fit_waste <= wasted_area))) {
				wasted_area = fit_waste;
				fit = r;
			}
		} /* find best fit in all empty rects */

		if (!fit)
			/* cannot allocate space for this lightmap fragment */
			return Atlas_ErrorDoesntFit;

		struct AtlasVec * const pos = (void*)((char*)context->pos + i * context->pos_stride);

		pos->x = fit->x;
		pos->y = fit->y;

		/* how to split */
		unsigned int rem_width = fit->w - item->x;
		unsigned int rem_height = fit->h - item->y;
		if (!rem_width && !rem_height) {
			fit->w = fit->h = 0;
			continue;
		}

		if (rem_width && rem_height && !first_empty)
			first_empty = rects + (rects_count++);

		/* split! */
		if (rem_width > rem_height) {
			if (rem_height) {
				first_empty->x = fit->x + item->x;
				first_empty->y = fit->y;
				first_empty->w = rem_width;
				first_empty->h = fit->h;
				fit->y += item->y;
				fit->w = item->x;
				fit->h = rem_height;
			} else {
				fit->x += item->x;
				fit->w = rem_width;
			}
		} else {
			if (rem_width) {
				first_empty->x = fit->x;
				first_empty->y = fit->y + item->y;
				first_empty->w = fit->w;
				first_empty->h = rem_height;
				fit->x += item->x;
				fit->w = rem_width;
				fit->h = item->y;
			} else {
				fit->y += item->y;
				fit->h = rem_height;
			}
		} /* split */
	} /* for all input rects */

	return Atlas_Success;
}
