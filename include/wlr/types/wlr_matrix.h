#ifndef WLR_TYPES_WLR_MATRIX_H
#define WLR_TYPES_WLR_MATRIX_H

#include <wayland-server.h>
#include <wlr/types/wlr_box.h>

void wlr_matrix_identity(float mat[static 9]);
void wlr_matrix_translate(float mat[static 9], float x, float y);
void wlr_matrix_scale(float mat[static 9], float x, float y);
void wlr_matrix_rotate(float mat[static 9], float rad);
void wlr_matrix_multiply(float mat[static 9], const float a[static 9],
	const float b[static 9]);
void wlr_matrix_transform(float mat[static 9],
	enum wl_output_transform transform);
void wlr_matrix_projection(float mat[static 9], int width, int height,
	enum wl_output_transform transform);
void wlr_matrix_project_box(float mat[static 9], const struct wlr_box *box,
	enum wl_output_transform transform, float rotation,
	const float projection[static 9]);

#endif
