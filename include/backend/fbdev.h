#ifndef BACKEND_FBDEV_H
#define BACKEND_FBDEV_H

#include <wlr/backend/fbdev.h>
#include <wlr/backend/interface.h>
#include <wlr/render/gles2.h>
#include <linux/fb.h>

#define FBDEV_DEFAULT_REFRESH (60 * 1000) // 60 Hz

struct wlr_fbdev_backend {
	struct wlr_backend backend;

	struct wlr_fbdev_backend *parent;

	struct wlr_egl priv_egl; // may be uninitialized
	struct wlr_egl *egl;
	struct wlr_renderer *renderer;
	struct wl_display *display;
	struct wl_list outputs;
	size_t last_output_num;
	struct wl_list input_devices;
	struct wl_listener display_destroy;
	struct wl_listener renderer_destroy;
	bool started;
	GLenum internal_format;

	int fd;
	struct wlr_session *session;
};

struct wlr_fbdev_output {
	struct wlr_output wlr_output;

	struct wlr_fbdev_backend *backend;
	struct wl_list link;

	GLuint fbo, rbo;

	struct wl_event_source *frame_timer;
	int frame_delay; // ms

	size_t fbmem_size;
	unsigned char *fbmem;

	struct fb_var_screeninfo scr_var;
	struct fb_fix_screeninfo scr_fix;
};

struct wlr_fbdev_input_device {
	struct wlr_input_device wlr_input_device;

	struct wlr_fbdev_backend *backend;
};

struct wlr_fbdev_backend *fbdev_backend_from_backend(
	struct wlr_backend *wlr_backend);

#endif
