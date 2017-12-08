#include <string.h>
#include <tgmath.h>
#include <wayland-server.h>
#include <wlr/render/matrix2.h>

extern inline float *wlr_matrix2_multiply(float *mat, const float *a, const float *b) {
	float res[9];

	res[0] = a[0]*b[0] + a[1]*b[3] + a[2]*b[6];
	res[1] = a[0]*b[1] + a[1]*b[4] + a[2]*b[7];
	res[2] = a[0]*b[2] + a[1]*b[5] + a[2]*b[8];

	res[3] = a[3]*b[0] + a[4]*b[3] + a[5]*b[6];
	res[4] = a[3]*b[1] + a[4]*b[4] + a[5]*b[7];
	res[5] = a[3]*b[2] + a[4]*b[5] + a[5]*b[8];

	res[6] = a[6]*b[0] + a[7]*b[3] + a[8]*b[6];
	res[7] = a[6]*b[1] + a[7]*b[4] + a[8]*b[7];
	res[8] = a[6]*b[2] + a[7]*b[5] + a[8]*b[8];

	memcpy(mat, res, sizeof(res));
	return mat;
}

float *wlr_matrix2_identity(float *mat) {
	static const float identity[9] = {
		1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	};
	memcpy(mat, identity, sizeof(identity));
	return mat;
}

float *wlr_matrix2_scale(float *mat, float x, float y) {
	float scale[9] = {
		x,    0.0f, 0.0f,
		0.0f, y,    0.0f,
		0.0f, 0.0f, 1.0f,
	};
	return wlr_matrix2_multiply(mat, mat, scale);
}

float *wlr_matrix2_scale_row(float *mat, size_t row, float scale) {
	row *= 3;
	mat[row + 0] *= scale;
	mat[row + 1] *= scale;
	mat[row + 2] *= scale;
	return mat;
}

float *wlr_matrix2_rotate(float *mat, float rad) {
	float rotate[9] = {
		cos(rad), -sin(rad), 0.0f,
		sin(rad),  cos(rad), 0.0f,
		0.0f,      0.0f,     1.0f,
	};
	return wlr_matrix2_multiply(mat, mat, rotate);
}

float *wlr_matrix2_translate(float *mat, float x, float y) {
	float translate[9] = {
		1.0f, 0.0f, x,
		0.0f, 1.0f, y,
		0.0f, 0.0f, 1.0f,
	};
	return wlr_matrix2_multiply(mat, mat, translate);
}

static const float transforms[][9] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = {
		1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_90] = {
		0.0f, -1.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_180] = {
		-1.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_270] = {
		0.0f, 1.0f, 0.0f,
		-1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED] = {
		-1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = {
		0.0f, -1.0f, 0.0f,
		-1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = {
		1.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = {
		0.0f, 1.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
};

float *wlr_matrix2_transform(float *mat, enum wl_output_transform transform) {
	return wlr_matrix2_multiply(mat, mat, transforms[transform]);
}
