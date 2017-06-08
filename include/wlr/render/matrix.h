#ifndef _WLR_RENDER_MATRIX_H
#define _WLR_RENDER_MATRIX_H

void wlr_matrix_identity(float (*output)[16]);
void wlr_matrix_translate(float (*output)[16], float x, float y, float z);
void wlr_matrix_scale(float (*output)[16], float x, float y, float z);
void wlr_matrix_rotate(float (*output)[16], float radians);
void wlr_matrix_mul(const float (*x)[16], const float (*y)[16], float (*product)[16]);

#endif
