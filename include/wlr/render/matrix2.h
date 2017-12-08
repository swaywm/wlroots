#ifndef WLR_RENDER_MATRIX_H
#define WLR_RENDER_MATRIX_H

#include <wayland-server.h>

/*
 * All matrix arguments must be pointers to an array of 9 floats
 */

/*
 * Multiplies a and b together, leaving the result in mat.
 * It is safe for mat to alias a or b.
 * Returns mat.
 */
float *wlr_matrix2_multiply(float *mat, const float *a, const float *b);

/*
 * Load the identity matrix into mat.
 * Returns mat.
 */
float *wlr_matrix2_identity(float *mat);

/*
 * Scales mat along the x and y axes.
 * Returns mat.
 */
float *wlr_matrix2_scale(float *mat, float x, float y);

/*
 * Scales a row in mat by scale.
 * Returns mat.
 */
float *wlr_matrix2_scale_row(float *mat, size_t row, float scale);

/*
 * Rotates mat by rad radians.
 * Returns mat.
 */
float *wlr_matrix2_rotate(float *mat, float rad);

/*
 * Translates max along the x and y axes.
 * Returns mat.
 */
float *wlr_matrix2_translate(float *mat, float x, float y);

/*
 * Transforms mat according to the wayland transform.
 * Returns mat.
 */
float *wlr_matrix2_transform(float *mat, enum wl_output_transform transform);

#endif
