#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

struct wl_compositor *compositor;
struct wl_subcompositor *subcompositor;

static void global(void *data, struct wl_registry *reg, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
		subcompositor = wl_registry_bind(reg, name, &wl_subcompositor_interface, 1);
	}
}

static void global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = global,
	.global_remove = global_remove,
};

int main(void)
{
	struct wl_display *display = wl_display_connect("test");

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	struct wl_surface *parent = wl_compositor_create_surface(compositor);

	struct wl_surface *surface = wl_compositor_create_surface(compositor);
	struct wl_subsurface *subsurface =
		wl_subcompositor_get_subsurface(subcompositor, surface, parent);

	wl_surface_damage(parent, 0, 0, 100, 100);
	wl_surface_commit(parent);

	wl_subsurface_set_position(subsurface, -100, -100);
	wl_surface_set_buffer_transform(parent, WL_OUTPUT_TRANSFORM_90);
	wl_surface_commit(parent);

	wl_subsurface_set_position(subsurface, 100, 100);
	wl_surface_commit(parent);

	while (wl_display_dispatch(display) != -1);

	const struct wl_interface *iface = NULL;
	uint32_t error = 0;
	wl_display_get_protocol_error(display, &iface, &error);

	if (error) {
		printf("%u %s\n", error, iface->name);
	}

	wl_display_disconnect(display);
}
