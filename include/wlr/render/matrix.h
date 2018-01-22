#ifndef WLR_RENDER_MATRIX_H
#define WLR_RENDER_MATRIX_H

#include <stdint.h>
#include <wlr/types/wlr_box.h>

void wlr_matrix_identity(float (*output)[16]);
void wlr_matrix_translate(float (*output)[16], float x, float y, float z);
void wlr_matrix_scale(float (*output)[16], float x, float y, float z);
void wlr_matrix_rotate(float (*output)[16], float radians);
void wlr_matrix_mul(const float (*x)[16], const float (*y)[16], float (*product)[16]);

enum wl_output_transform;
void wlr_matrix_transform(float mat[static 16],
		enum wl_output_transform transform);
void wlr_matrix_texture(float mat[static 16], int32_t width, int32_t height,
		enum wl_output_transform transform);
void wlr_matrix_project_box(float (*mat)[16], struct wlr_box *box,
		enum wl_output_transform transform, float rotation, float
		(*projection)[16]);

#endif
