#include <math.h>
#include <stdlib.h>
#include <wlr/util/region.h>
#include "util/defs.h"

WLR_API
void wlr_region_scale(pixman_region32_t *dst, pixman_region32_t *src,
		float scale) {
	if (scale == 1) {
		pixman_region32_copy(dst, src);
		return;
	}

	int nrects;
	pixman_box32_t *src_rects = pixman_region32_rectangles(src, &nrects);

	pixman_box32_t *dst_rects = malloc(nrects * sizeof(pixman_box32_t));
	if (dst_rects == NULL) {
		return;
	}

	for (int i = 0; i < nrects; ++i) {
		dst_rects[i].x1 = floor(src_rects[i].x1 * scale);
		dst_rects[i].x2 = ceil(src_rects[i].x2 * scale);
		dst_rects[i].y1 = floor(src_rects[i].y1 * scale);
		dst_rects[i].y2 = ceil(src_rects[i].y2 * scale);
	}

	pixman_region32_fini(dst);
	pixman_region32_init_rects(dst, dst_rects, nrects);
	free(dst_rects);
}

WLR_API
void wlr_region_transform(pixman_region32_t *dst, pixman_region32_t *src,
		enum wl_output_transform transform, int width, int height) {
	if (transform == WL_OUTPUT_TRANSFORM_NORMAL) {
		pixman_region32_copy(dst, src);
		return;
	}

	int nrects;
	pixman_box32_t *src_rects = pixman_region32_rectangles(src, &nrects);

	pixman_box32_t *dst_rects = malloc(nrects * sizeof(pixman_box32_t));
	if (dst_rects == NULL) {
		return;
	}

	for (int i = 0; i < nrects; ++i) {
		switch (transform) {
		case WL_OUTPUT_TRANSFORM_NORMAL:
			dst_rects[i].x1 = src_rects[i].x1;
			dst_rects[i].y1 = src_rects[i].y1;
			dst_rects[i].x2 = src_rects[i].x2;
			dst_rects[i].y2 = src_rects[i].y2;
			break;
		case WL_OUTPUT_TRANSFORM_90:
			dst_rects[i].x1 = src_rects[i].y1;
			dst_rects[i].y1 = width - src_rects[i].x2;
			dst_rects[i].x2 = src_rects[i].y2;
			dst_rects[i].y2 = width - src_rects[i].x1;
			break;
		case WL_OUTPUT_TRANSFORM_180:
			dst_rects[i].x1 = width - src_rects[i].x2;
			dst_rects[i].y1 = height - src_rects[i].y2;
			dst_rects[i].x2 = width - src_rects[i].x1;
			dst_rects[i].y2 = height - src_rects[i].y1;
			break;
		case WL_OUTPUT_TRANSFORM_270:
			dst_rects[i].x1 = height - src_rects[i].y2;
			dst_rects[i].y1 = src_rects[i].x1;
			dst_rects[i].x2 = height - src_rects[i].y1;
			dst_rects[i].y2 = src_rects[i].x2;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED:
			dst_rects[i].x1 = width - src_rects[i].x2;
			dst_rects[i].y1 = src_rects[i].y1;
			dst_rects[i].x2 = width - src_rects[i].x1;
			dst_rects[i].y2 = src_rects[i].y2;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_90:
			dst_rects[i].x1 = height - src_rects[i].y2;
			dst_rects[i].y1 = width - src_rects[i].x2;
			dst_rects[i].x2 = height - src_rects[i].y1;
			dst_rects[i].y2 = width - src_rects[i].x1;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_180:
			dst_rects[i].x1 = src_rects[i].x1;
			dst_rects[i].y1 = height - src_rects[i].y2;
			dst_rects[i].x2 = src_rects[i].x2;
			dst_rects[i].y2 = height - src_rects[i].y1;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_270:
			dst_rects[i].x1 = src_rects[i].y1;
			dst_rects[i].y1 = src_rects[i].x1;
			dst_rects[i].x2 = src_rects[i].y2;
			dst_rects[i].y2 = src_rects[i].x2;
			break;
		}
	}

	pixman_region32_fini(dst);
	pixman_region32_init_rects(dst, dst_rects, nrects);
	free(dst_rects);
}

WLR_API
void wlr_region_expand(pixman_region32_t *dst, pixman_region32_t *src,
		int distance) {
	if (distance == 0) {
		pixman_region32_copy(dst, src);
		return;
	}

	int nrects;
	pixman_box32_t *src_rects = pixman_region32_rectangles(src, &nrects);

	pixman_box32_t *dst_rects = malloc(nrects * sizeof(pixman_box32_t));
	if (dst_rects == NULL) {
		return;
	}

	for (int i = 0; i < nrects; ++i) {
		dst_rects[i].x1 = src_rects[i].x1 - distance;
		dst_rects[i].x2 = src_rects[i].x2 + distance;
		dst_rects[i].y1 = src_rects[i].y1 - distance;
		dst_rects[i].y2 = src_rects[i].y2 + distance;
	}

	pixman_region32_fini(dst);
	pixman_region32_init_rects(dst, dst_rects, nrects);
	free(dst_rects);
}
