#include <math.h>
#include <stdlib.h>
#include <wlr/util/region.h>

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

void wlr_region_rotated_bounds(pixman_region32_t *dst, pixman_region32_t *src,
		float rotation, int ox, int oy) {
	if (rotation == 0) {
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
		double x1 = src_rects[i].x1 - ox;
		double y1 = src_rects[i].y1 - oy;
		double x2 = src_rects[i].x2 - ox;
		double y2 = src_rects[i].y2 - oy;

		double rx1 = x1 * cos(rotation) - y1 * sin(rotation);
		double ry1 = x1 * sin(rotation) + y1 * cos(rotation);

		double rx2 = x2 * cos(rotation) - y1 * sin(rotation);
		double ry2 = x2 * sin(rotation) + y1 * cos(rotation);

		double rx3 = x2 * cos(rotation) - y2 * sin(rotation);
		double ry3 = x2 * sin(rotation) + y2 * cos(rotation);

		double rx4 = x1 * cos(rotation) - y2 * sin(rotation);
		double ry4 = x1 * sin(rotation) + y2 * cos(rotation);

		x1 = fmin(fmin(rx1, rx2), fmin(rx3, rx4));
		y1 = fmin(fmin(ry1, ry2), fmin(ry3, ry4));
		x2 = fmax(fmax(rx1, rx2), fmax(rx3, rx4));
		y2 = fmax(fmax(ry1, ry2), fmax(ry3, ry4));

		dst_rects[i].x1 = floor(ox + x1);
		dst_rects[i].x2 = ceil(ox + x2);
		dst_rects[i].y1 = floor(oy + y1);
		dst_rects[i].y2 = ceil(oy + y2);
	}

	pixman_region32_fini(dst);
	pixman_region32_init_rects(dst, dst_rects, nrects);
	free(dst_rects);
}
