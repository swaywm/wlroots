/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_GLES2_H
#define WLR_RENDER_GLES2_H

#include <GLES2/gl2.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_egl_context;

struct wlr_renderer *wlr_gles2_renderer_create_with_drm_fd(int drm_fd);

/**
 * Returns the current EGL context
 * The pointer returned is not owned by the caller
 */
struct wlr_egl_context *wlr_gles2_renderer_get_egl_context(
	struct wlr_renderer *wlr_renderer);

bool wlr_gles2_renderer_check_ext(struct wlr_renderer *renderer,
	const char *ext);
/**
 * Returns the OpenGL FBO of current buffer.
 */
GLuint wlr_gles2_renderer_get_current_fbo(struct wlr_renderer *wlr_renderer);

struct wlr_gles2_texture_attribs {
	GLenum target; /* either GL_TEXTURE_2D or GL_TEXTURE_EXTERNAL_OES */
	GLuint tex;

	bool inverted_y;
	bool has_alpha;
};

bool wlr_renderer_is_gles2(struct wlr_renderer *wlr_renderer);
bool wlr_texture_is_gles2(struct wlr_texture *texture);
void wlr_gles2_texture_get_attribs(struct wlr_texture *texture,
	struct wlr_gles2_texture_attribs *attribs);

#endif
