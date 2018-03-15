#ifndef WLR_TYPES_WLR_MATRIX_H
#define WLR_TYPES_WLR_MATRIX_H

#include <stdint.h>
#include <wlr/types/wlr_box.h>

void wlr_matrix_identity(float mat[static 16]);
void wlr_matrix_translate(float mat[static 16], float x, float y, float z);
void wlr_matrix_scale(float mat[static 16], float x, float y, float z);
void wlr_matrix_rotate(float mat[static 16], float radians);
void wlr_matrix_mul(float mat[static 16], const float x[static 16],
	const float y[static 16]);

enum wl_output_transform;
void wlr_matrix_transform(float mat[static 16],
	enum wl_output_transform transform);
void wlr_matrix_texture(float mat[static 16], int32_t width, int32_t height,
	enum wl_output_transform transform);
void wlr_matrix_project_box(float mat[static 16], const struct wlr_box *box,
	enum wl_output_transform transform, float rotation,
	const float projection[static 16]);

#endif
