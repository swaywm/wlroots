#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <wayland-server-protocol.h>
#include <wlr/render/matrix.h>
#include <pixman.h>

/* Obtains the index for the given row/column */
static inline int mind(int row, int col) {
	return (row - 1) * 4 + col - 1;
}

void wlr_matrix_identity(float (*output)[16]) {
	static const float identity[16] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	memcpy(*output, identity, sizeof(identity));
}

void wlr_matrix_translate(float (*output)[16], float x, float y, float z) {
	wlr_matrix_identity(output);
	(*output)[mind(1, 4)] = x;
	(*output)[mind(2, 4)] = y;
	(*output)[mind(3, 4)] = z;
}

void wlr_matrix_scale(float (*output)[16], float x, float y, float z) {
	wlr_matrix_identity(output);
	(*output)[mind(1, 1)] = x;
	(*output)[mind(2, 2)] = y;
	(*output)[mind(3, 3)] = z;
}

void wlr_matrix_rotate(float (*output)[16], float radians) {
	wlr_matrix_identity(output);
	float _cos = cosf(radians);
	float _sin = sinf(radians);
	(*output)[mind(1, 1)] = _cos;
	(*output)[mind(1, 2)] = _sin;
	(*output)[mind(2, 1)] = -_sin;
	(*output)[mind(2, 2)] = _cos;
}

void wlr_matrix_mul(const float (*x)[16], const float (*y)[16], float (*product)[16]) {
	float _product[16] = {
		(*x)[mind(1, 1)] * (*y)[mind(1, 1)] + (*x)[mind(1, 2)] * (*y)[mind(2, 1)] +
			(*x)[mind(1, 3)] * (*y)[mind(3, 1)] + (*x)[mind(1, 4)] * (*y)[mind(4, 1)],
		(*x)[mind(1, 1)] * (*y)[mind(1, 2)] + (*x)[mind(1, 2)] * (*y)[mind(2, 2)] +
			(*x)[mind(1, 3)] * (*y)[mind(3, 2)] + (*x)[mind(1, 4)] * (*y)[mind(4, 2)],
		(*x)[mind(1, 1)] * (*y)[mind(1, 3)] + (*x)[mind(1, 2)] * (*y)[mind(2, 3)] +
			(*x)[mind(1, 3)] * (*y)[mind(3, 3)] + (*x)[mind(1, 4)] * (*y)[mind(4, 3)],
		(*x)[mind(1, 1)] * (*y)[mind(1, 4)] + (*x)[mind(1, 2)] * (*y)[mind(2, 4)] +
			(*x)[mind(1, 4)] * (*y)[mind(3, 4)] + (*x)[mind(1, 4)] * (*y)[mind(4, 4)],

		(*x)[mind(2, 1)] * (*y)[mind(1, 1)] + (*x)[mind(2, 2)] * (*y)[mind(2, 1)] +
			(*x)[mind(2, 3)] * (*y)[mind(3, 1)] + (*x)[mind(2, 4)] * (*y)[mind(4, 1)],
		(*x)[mind(2, 1)] * (*y)[mind(1, 2)] + (*x)[mind(2, 2)] * (*y)[mind(2, 2)] +
			(*x)[mind(2, 3)] * (*y)[mind(3, 2)] + (*x)[mind(2, 4)] * (*y)[mind(4, 2)],
		(*x)[mind(2, 1)] * (*y)[mind(1, 3)] + (*x)[mind(2, 2)] * (*y)[mind(2, 3)] +
			(*x)[mind(2, 3)] * (*y)[mind(3, 3)] + (*x)[mind(2, 4)] * (*y)[mind(4, 3)],
		(*x)[mind(2, 1)] * (*y)[mind(1, 4)] + (*x)[mind(2, 2)] * (*y)[mind(2, 4)] +
			(*x)[mind(2, 4)] * (*y)[mind(3, 4)] + (*x)[mind(2, 4)] * (*y)[mind(4, 4)],

		(*x)[mind(3, 1)] * (*y)[mind(1, 1)] + (*x)[mind(3, 2)] * (*y)[mind(2, 1)] +
			(*x)[mind(3, 3)] * (*y)[mind(3, 1)] + (*x)[mind(3, 4)] * (*y)[mind(4, 1)],
		(*x)[mind(3, 1)] * (*y)[mind(1, 2)] + (*x)[mind(3, 2)] * (*y)[mind(2, 2)] +
			(*x)[mind(3, 3)] * (*y)[mind(3, 2)] + (*x)[mind(3, 4)] * (*y)[mind(4, 2)],
		(*x)[mind(3, 1)] * (*y)[mind(1, 3)] + (*x)[mind(3, 2)] * (*y)[mind(2, 3)] +
			(*x)[mind(3, 3)] * (*y)[mind(3, 3)] + (*x)[mind(3, 4)] * (*y)[mind(4, 3)],
		(*x)[mind(3, 1)] * (*y)[mind(1, 4)] + (*x)[mind(3, 2)] * (*y)[mind(2, 4)] +
			(*x)[mind(3, 4)] * (*y)[mind(3, 4)] + (*x)[mind(3, 4)] * (*y)[mind(4, 4)],

		(*x)[mind(4, 1)] * (*y)[mind(1, 1)] + (*x)[mind(4, 2)] * (*y)[mind(2, 1)] +
			(*x)[mind(4, 3)] * (*y)[mind(3, 1)] + (*x)[mind(4, 4)] * (*y)[mind(4, 1)],
		(*x)[mind(4, 1)] * (*y)[mind(1, 2)] + (*x)[mind(4, 2)] * (*y)[mind(2, 2)] +
			(*x)[mind(4, 3)] * (*y)[mind(3, 2)] + (*x)[mind(4, 4)] * (*y)[mind(4, 2)],
		(*x)[mind(4, 1)] * (*y)[mind(1, 3)] + (*x)[mind(4, 2)] * (*y)[mind(2, 3)] +
			(*x)[mind(4, 3)] * (*y)[mind(3, 3)] + (*x)[mind(4, 4)] * (*y)[mind(4, 3)],
		(*x)[mind(4, 1)] * (*y)[mind(1, 4)] + (*x)[mind(4, 2)] * (*y)[mind(2, 4)] +
			(*x)[mind(4, 4)] * (*y)[mind(3, 4)] + (*x)[mind(4, 4)] * (*y)[mind(4, 4)],
	};
	memcpy(*product, _product, sizeof(_product));
}

static const float transforms[][4] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = {
		1.0f, 0.0f,
		0.0f, -1.0f,
	},
	[WL_OUTPUT_TRANSFORM_90] = {
		0.0f, -1.0f,
		-1.0f, 0.0f,
	},
	[WL_OUTPUT_TRANSFORM_180] = {
		-1.0f, 0.0f,
		0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_270] = {
		0.0f, 1.0f,
		1.0f, 0.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED] = {
		-1.0f, 0.0f,
		0.0f, -1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = {
		0.0f, 1.0f,
		-1.0f, 0.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = {
		1.0f, 0.0f,
		0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = {
		0.0f, -1.0f,
		1.0f, 0.0f,
	},
};

// Equivilent to glOrtho(0, width, 0, height, 1, -1) with the transform applied
void wlr_matrix_texture(float mat[static 16], int32_t width, int32_t height,
		enum wl_output_transform transform) {
	memset(mat, 0, sizeof(*mat) * 16);

	const float *t = transforms[transform];
	float x = 2.0f / width;
	float y = 2.0f / height;

	// Rotation + relection
	mat[0] = x * t[0];
	mat[1] = x * t[1];
	mat[4] = y * t[2];
	mat[5] = y * t[3];

	// Translation
	mat[3] = -copysign(1.0f, mat[0] + mat[1]);
	mat[7] = -copysign(1.0f, mat[4] + mat[5]);

	// Identity
	mat[10] = 1.0f;
	mat[15] = 1.0f;
}

struct wlr_vector {
	float f[4];
};

/* v <- m * v */
static void wlr_matrix_transform(float matrix[16], struct wlr_vector *v) {
	int i, j;
	struct wlr_vector t;

	for (i = 0; i < 4; i++) {
		t.f[i] = 0;
		for (j = 0; j < 4; j++)
			t.f[i] += v->f[j] * matrix[i + j * 4];
	}

	*v = t;
}

/*
 * Transform a region by a matrix, restricted to axis-aligned transformations
 *
 * Warning: This function does not work for projective, affine, or matrices
 * that encode arbitrary rotations. Only 90-degree step rotations are
 * supported.
 */
void wlr_matrix_transform_region(pixman_region32_t *dest,
		float matrix[16],
		pixman_region32_t *src) {
	pixman_box32_t *src_rects, *dest_rects;
	int nrects, i;

	src_rects = pixman_region32_rectangles(src, &nrects);
	dest_rects = malloc(nrects * sizeof(*dest_rects));
	if (!dest_rects) {
		return;
	}

	for (i = 0; i < nrects; i++) {
		struct wlr_vector vec1 = {{
			src_rects[i].x1, src_rects[i].y1, 0, 1
		}};
		wlr_matrix_transform(matrix, &vec1);
		vec1.f[0] /= vec1.f[3];
		vec1.f[1] /= vec1.f[3];

		struct wlr_vector vec2 = {{
			src_rects[i].x2, src_rects[i].y2, 0, 1
		}};
		wlr_matrix_transform(matrix, &vec2);
		vec2.f[0] /= vec2.f[3];
		vec2.f[1] /= vec2.f[3];

		if (vec1.f[0] < vec2.f[0]) {
			dest_rects[i].x1 = floor(vec1.f[0]);
			dest_rects[i].x2 = ceil(vec2.f[0]);
		} else {
			dest_rects[i].x1 = floor(vec2.f[0]);
			dest_rects[i].x2 = ceil(vec1.f[0]);
		}

		if (vec1.f[1] < vec2.f[1]) {
			dest_rects[i].y1 = floor(vec1.f[1]);
			dest_rects[i].y2 = ceil(vec2.f[1]);
		} else {
			dest_rects[i].y1 = floor(vec2.f[1]);
			dest_rects[i].y2 = ceil(vec1.f[1]);
		}
	}

	pixman_region32_clear(dest);
	pixman_region32_init_rects(dest, dest_rects, nrects);
	free(dest_rects);
}
