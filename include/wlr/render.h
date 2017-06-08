#ifndef _WLR_RENDER_H
#define _WLR_RENDER_H
#include <stdint.h>
#include <wayland-server-protocol.h>

struct wlr_surface;
struct wlr_surface *wlr_surface_init();
void wlr_surface_attach_pixels(struct wlr_surface *surf, uint32_t format,
		int width, int height, const unsigned char *pixels);
void wlr_surface_attach_shm(struct wlr_surface *surf, uint32_t format,
		struct wl_shm_buffer *shm);
// TODO: EGL
void wlr_surface_destroy(struct wlr_surface *tex);

struct wlr_shader;
struct wlr_shader *wlr_shader_init(const char *vertex);
bool wlr_shader_add_format(struct wlr_shader *shader, uint32_t format,
		const char *frag);
bool wlr_shader_use(struct wlr_shader *shader, uint32_t format);
void wlr_shader_destroy(struct wlr_shader *shader);

struct wlr_renderer;
struct wlr_renderer *wlr_renderer_init();
void wlr_renderer_set_shader(struct wlr_renderer *renderer,
		struct wlr_shader *shader);
bool wlr_render_quad(struct wlr_renderer *renderer,
		struct wlr_surface *surf, float (*transform)[16],
		float x, float y);
void wlr_renderer_destroy(struct wlr_renderer *renderer);

#endif
