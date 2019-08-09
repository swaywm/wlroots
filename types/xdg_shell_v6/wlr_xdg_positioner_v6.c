#include <assert.h>
#include <stdlib.h>
#include "types/wlr_xdg_shell_v6.h"

static const struct zxdg_positioner_v6_interface
	zxdg_positioner_v6_implementation;

struct wlr_xdg_positioner_v6_resource *get_xdg_positioner_v6_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zxdg_positioner_v6_interface,
		&zxdg_positioner_v6_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_positioner_destroy(struct wl_resource *resource) {
	struct wlr_xdg_positioner_v6_resource *positioner =
		get_xdg_positioner_v6_from_resource(resource);
	free(positioner);
}

static void xdg_positioner_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void xdg_positioner_handle_set_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_positioner_v6_resource *positioner =
		get_xdg_positioner_v6_from_resource(resource);

	if (width < 1 || height < 1) {
		wl_resource_post_error(resource,
			ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
			"width and height must be positive and non-zero");
		return;
	}

	positioner->attrs.size.width = width;
	positioner->attrs.size.height = height;
}

static void xdg_positioner_handle_set_anchor_rect(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_xdg_positioner_v6_resource *positioner =
		get_xdg_positioner_v6_from_resource(resource);

	if (width < 1 || height < 1) {
		wl_resource_post_error(resource,
			ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
			"width and height must be positive and non-zero");
		return;
	}

	positioner->attrs.anchor_rect.x = x;
	positioner->attrs.anchor_rect.y = y;
	positioner->attrs.anchor_rect.width = width;
	positioner->attrs.anchor_rect.height = height;
}

static void xdg_positioner_handle_set_anchor(struct wl_client *client,
		struct wl_resource *resource, uint32_t anchor) {
	struct wlr_xdg_positioner_v6_resource *positioner =
		get_xdg_positioner_v6_from_resource(resource);

	if (((anchor & ZXDG_POSITIONER_V6_ANCHOR_TOP ) &&
				(anchor & ZXDG_POSITIONER_V6_ANCHOR_BOTTOM)) ||
			((anchor & ZXDG_POSITIONER_V6_ANCHOR_LEFT) &&
				(anchor & ZXDG_POSITIONER_V6_ANCHOR_RIGHT))) {
		wl_resource_post_error(resource,
			ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
			"same-axis values are not allowed");
		return;
	}

	positioner->attrs.anchor = anchor;
}

static void xdg_positioner_handle_set_gravity(struct wl_client *client,
		struct wl_resource *resource, uint32_t gravity) {
	struct wlr_xdg_positioner_v6_resource *positioner =
		get_xdg_positioner_v6_from_resource(resource);

	if (((gravity & ZXDG_POSITIONER_V6_GRAVITY_TOP) &&
				(gravity & ZXDG_POSITIONER_V6_GRAVITY_BOTTOM)) ||
			((gravity & ZXDG_POSITIONER_V6_GRAVITY_LEFT) &&
				(gravity & ZXDG_POSITIONER_V6_GRAVITY_RIGHT))) {
		wl_resource_post_error(resource,
			ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
			"same-axis values are not allowed");
		return;
	}

	positioner->attrs.gravity = gravity;
}

static void xdg_positioner_handle_set_constraint_adjustment(
		struct wl_client *client, struct wl_resource *resource,
		uint32_t constraint_adjustment) {
	struct wlr_xdg_positioner_v6_resource *positioner =
		get_xdg_positioner_v6_from_resource(resource);

	positioner->attrs.constraint_adjustment = constraint_adjustment;
}

static void xdg_positioner_handle_set_offset(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y) {
	struct wlr_xdg_positioner_v6_resource *positioner =
		get_xdg_positioner_v6_from_resource(resource);

	positioner->attrs.offset.x = x;
	positioner->attrs.offset.y = y;
}

static const struct zxdg_positioner_v6_interface
		zxdg_positioner_v6_implementation = {
	.destroy = xdg_positioner_handle_destroy,
	.set_size = xdg_positioner_handle_set_size,
	.set_anchor_rect = xdg_positioner_handle_set_anchor_rect,
	.set_anchor = xdg_positioner_handle_set_anchor,
	.set_gravity = xdg_positioner_handle_set_gravity,
	.set_constraint_adjustment =
		xdg_positioner_handle_set_constraint_adjustment,
	.set_offset = xdg_positioner_handle_set_offset,
};

struct wlr_box wlr_xdg_positioner_v6_get_geometry(
		struct wlr_xdg_positioner_v6 *positioner) {
	struct wlr_box geometry = {
		.x = positioner->offset.x,
		.y = positioner->offset.y,
		.width = positioner->size.width,
		.height = positioner->size.height,
	};

	if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_TOP) {
		geometry.y += positioner->anchor_rect.y;
	} else if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_BOTTOM) {
		geometry.y +=
			positioner->anchor_rect.y + positioner->anchor_rect.height;
	} else {
		geometry.y +=
			positioner->anchor_rect.y + positioner->anchor_rect.height / 2;
	}

	if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_LEFT) {
		geometry.x += positioner->anchor_rect.x;
	} else if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_RIGHT) {
		geometry.x += positioner->anchor_rect.x + positioner->anchor_rect.width;
	} else {
		geometry.x +=
			positioner->anchor_rect.x + positioner->anchor_rect.width / 2;
	}

	if (positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_TOP) {
		geometry.y -= geometry.height;
	} else if (!(positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_BOTTOM)) {
		geometry.y -= geometry.height / 2;
	}

	if (positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_LEFT) {
		geometry.x -= geometry.width;
	} else if (!(positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_RIGHT)) {
		geometry.x -= geometry.width / 2;
	}

	if (positioner->constraint_adjustment ==
			ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_NONE) {
		return geometry;
	}

	return geometry;
}

void wlr_positioner_v6_invert_x(struct wlr_xdg_positioner_v6 *positioner) {
	if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_LEFT) {
		positioner->anchor &= ~ZXDG_POSITIONER_V6_ANCHOR_LEFT;
		positioner->anchor |= ZXDG_POSITIONER_V6_ANCHOR_RIGHT;
	} else if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_RIGHT) {
		positioner->anchor &= ~ZXDG_POSITIONER_V6_ANCHOR_RIGHT;
		positioner->anchor |= ZXDG_POSITIONER_V6_ANCHOR_LEFT;
	}

	if (positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_RIGHT) {
		positioner->gravity &= ~ZXDG_POSITIONER_V6_GRAVITY_RIGHT;
		positioner->gravity |= ZXDG_POSITIONER_V6_GRAVITY_LEFT;
	} else if (positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_LEFT) {
		positioner->gravity &= ~ZXDG_POSITIONER_V6_GRAVITY_LEFT;
		positioner->gravity |= ZXDG_POSITIONER_V6_GRAVITY_RIGHT;
	}
}

void wlr_positioner_v6_invert_y(
		struct wlr_xdg_positioner_v6 *positioner) {
	if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_TOP) {
		positioner->anchor &= ~ZXDG_POSITIONER_V6_ANCHOR_TOP;
		positioner->anchor |= ZXDG_POSITIONER_V6_ANCHOR_BOTTOM;
	} else if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_BOTTOM) {
		positioner->anchor &= ~ZXDG_POSITIONER_V6_ANCHOR_BOTTOM;
		positioner->anchor |= ZXDG_POSITIONER_V6_ANCHOR_TOP;
	}

	if (positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_TOP) {
		positioner->gravity &= ~ZXDG_POSITIONER_V6_GRAVITY_TOP;
		positioner->gravity |= ZXDG_POSITIONER_V6_GRAVITY_BOTTOM;
	} else if (positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_BOTTOM) {
		positioner->gravity &= ~ZXDG_POSITIONER_V6_GRAVITY_BOTTOM;
		positioner->gravity |= ZXDG_POSITIONER_V6_GRAVITY_TOP;
	}
}

void create_xdg_positioner_v6(struct wlr_xdg_client_v6 *client, uint32_t id) {
	struct wlr_xdg_positioner_v6_resource *positioner =
		calloc(1, sizeof(struct wlr_xdg_positioner_v6_resource));
	if (positioner == NULL) {
		wl_client_post_no_memory(client->client);
		return;
	}

	positioner->resource = wl_resource_create(client->client,
		&zxdg_positioner_v6_interface,
		wl_resource_get_version(client->resource), id);
	if (positioner->resource == NULL) {
		free(positioner);
		wl_client_post_no_memory(client->client);
		return;
	}
	wl_resource_set_implementation(positioner->resource,
		&zxdg_positioner_v6_implementation, positioner, xdg_positioner_destroy);
}
