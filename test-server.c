#include <signal.h>
#include <wayland-server.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>

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

	struct wlr_compositor *comp = wlr_compositor_create(display, NULL);
	struct wlr_subcompositor *subcomp = wlr_subcompositor_create(comp);

	(void)subcomp;
	wl_display_run(display);

	wl_display_destroy_clients(display);
	wl_display_destroy(display);
}
