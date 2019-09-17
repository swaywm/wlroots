#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>

static void manager_free_textures(struct wlr_xcursor_manager *manager) {
	struct wlr_xcursor_manager_theme *iter;
	wl_list_for_each(iter, &manager->scaled_themes, link) {
		struct wlr_xcursor_theme *theme = iter->theme;
		for (unsigned i = 0; i < theme->cursor_count; ++i) {
			struct wlr_xcursor *xcursor = theme->cursors[i];
			for (unsigned j = 0; j < xcursor->image_count; ++j) {
				struct wlr_xcursor_image *img = xcursor->images[j];

				wlr_texture_destroy(img->userdata);
				img->userdata = NULL;
			}
		}
	}
}

static void handle_renderer_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xcursor_manager *manager =
		wl_container_of(listener, manager, renderer_destroy);
	manager->renderer = NULL;
	manager_free_textures(manager);
}

struct wlr_xcursor_manager *wlr_xcursor_manager_create(const char *name,
		uint32_t size, struct wlr_renderer *renderer) {
	struct wlr_xcursor_manager *manager =
		calloc(1, sizeof(struct wlr_xcursor_manager));
	if (manager == NULL) {
		return NULL;
	}
	if (name != NULL) {
		manager->name = strdup(name);
	}
	manager->size = size;
	wl_list_init(&manager->scaled_themes);
	if (renderer) {
		manager->renderer = renderer;
		manager->renderer_destroy.notify = handle_renderer_destroy;
		wl_signal_add(&renderer->events.destroy, &manager->renderer_destroy);
	}
	return manager;
}

void wlr_xcursor_manager_destroy(struct wlr_xcursor_manager *manager) {
	if (manager == NULL) {
		return;
	}
	if (manager->renderer) {
		manager_free_textures(manager);
	}

	struct wlr_xcursor_manager_theme *theme, *tmp;
	wl_list_for_each_safe(theme, tmp, &manager->scaled_themes, link) {
		wlr_xcursor_theme_destroy(theme->theme);

		wl_list_remove(&theme->link);
		free(theme);
	}
	if (manager->renderer) {
		wl_list_remove(&manager->renderer_destroy.link);
	}
	free(manager->name);
	free(manager);
}

int wlr_xcursor_manager_load(struct wlr_xcursor_manager *manager,
		float scale) {
	struct wlr_xcursor_manager_theme *theme;
	wl_list_for_each(theme, &manager->scaled_themes, link) {
		if (theme->scale == scale) {
			return 0;
		}
	}

	theme = calloc(1, sizeof(struct wlr_xcursor_manager_theme));
	if (theme == NULL) {
		return 1;
	}
	theme->scale = scale;
	theme->theme = wlr_xcursor_theme_load(manager->name, manager->size * scale);
	if (theme->theme == NULL) {
		free(theme);
		return 1;
	}
	wl_list_insert(&manager->scaled_themes, &theme->link);
	return 0;
}

struct wlr_xcursor *wlr_xcursor_manager_get_xcursor(
		struct wlr_xcursor_manager *manager, const char *name, float scale) {
	struct wlr_xcursor_manager_theme *theme;
	wl_list_for_each(theme, &manager->scaled_themes, link) {
		if (theme->scale == scale) {
			return wlr_xcursor_theme_get_cursor(theme->theme, name);
		}
	}
	return NULL;
}

struct wlr_texture *wlr_xcursor_manager_get_texture(
		struct wlr_xcursor_manager *manager,
		struct wlr_xcursor_image *image) {
	if (!manager->renderer) {
		return NULL;
	}

	if (!image->userdata) {
		image->userdata = wlr_texture_from_pixels(manager->renderer,
			WL_SHM_FORMAT_ARGB8888, image->width * 4, image->width,
			image->height, image->buffer);
	}

	return image->userdata;
}
