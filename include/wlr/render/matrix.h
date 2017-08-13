#ifndef _WLR_RENDER_MATRIX_H
#define _WLR_RENDER_MATRIX_H

#include <stdint.h>
#include <pixman.h>

void wlr_matrix_identity(float (*output)[16]);
void wlr_matrix_translate(float (*output)[16], float x, float y, float z);
void wlr_matrix_scale(float (*output)[16], float x, float y, float z);
void wlr_matrix_rotate(float (*output)[16], float radians);
void wlr_matrix_mul(const float (*x)[16], const float (*y)[16], float (*product)[16]);

enum wl_output_transform;
void wlr_matrix_texture(float mat[static 16], int32_t width, int32_t height,
		enum wl_output_transform transform);
void wlr_matrix_transform_region(pixman_region32_t *dest,
		float matrix[16],
		pixman_region32_t *src);

#endif
