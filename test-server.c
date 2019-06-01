#include <fcntl.h>
#include <gbm.h>
#include <signal.h>
#include <unistd.h>
#include <wayland-server.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/egl.h>

struct wl_display *display;

static void sigint(int signo)
{
	wl_display_terminate(display);
}

int main(void)
{
	display = wl_display_create();
	wl_display_add_socket(display, "test");

	signal(SIGINT, sigint);

	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);

	struct wlr_egl egl = {0};
	struct wlr_renderer *renderer = wlr_renderer_autocreate(&egl,
		EGL_PLATFORM_GBM_MESA, gbm, NULL, GBM_FORMAT_XRGB8888);

	struct wlr_compositor *comp = wlr_compositor_create(display);
	struct wlr_subcompositor *subcomp = wlr_subcompositor_create(comp);

	wlr_renderer_set_compositor(renderer, comp);

	(void)subcomp;
	wl_display_run(display);

	wlr_renderer_destroy(renderer);
	wlr_egl_finish(&egl);
	gbm_device_destroy(gbm);
	close(fd);

	wl_display_destroy_clients(display);
	wl_display_destroy(display);
}
