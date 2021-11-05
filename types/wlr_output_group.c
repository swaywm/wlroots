#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/types/wlr_output_group.h>

struct wlr_output_group_child {
	struct wlr_output *output;
	struct wl_list link; // wlr_output_group.children

	struct wl_listener output_destroy;
};

static const struct wlr_output_impl output_impl;

static struct wlr_output_group *group_from_output(struct wlr_output *output) {
	assert(output->impl == &output_impl);
	return (struct wlr_output_group *)output;
}

static void group_destroy(struct wlr_output *output) {
	struct wlr_output_group *group = group_from_output(output);

	wl_list_remove(&group->main_output_destroy.link);

	struct wlr_output_group_child *child, *child_tmp;
	wl_list_for_each_safe(child, child_tmp, &group->children, link) {
		wlr_output_group_child_destroy(child);
	}

	free(group);
}

static void output_apply(struct wlr_output *output,
		struct wlr_output_state *state) {
	if (state->committed & WLR_OUTPUT_STATE_BUFFER) {
		wlr_output_attach_buffer(output, state->buffer);
	}
	// TODO: everything else
}

static bool group_commit(struct wlr_output *output) {
	struct wlr_output_group *group = group_from_output(output);

	output_apply(group->main_output, &output->pending);

	struct wlr_output_group_child *child;
	wl_list_for_each(child, &group->children, link) {
		output_apply(child->output, &output->pending);
	}

	// TODO: perform a backend-wide commit if possible
	if (!wlr_output_commit(group->main_output)) {
		goto error;
	}

	wl_list_for_each(child, &group->children, link) {
		if (!wlr_output_commit(child->output)) {
			goto error;
		}
	}

	// TODO: update our current state

	return true;

error:
	wlr_output_rollback(group->main_output);

	wl_list_for_each(child, &group->children, link) {
		wlr_output_rollback(child->output);
	}

	return false;
}

static const struct wlr_drm_format_set *group_get_primary_formats(
		struct wlr_output *output, uint32_t buffer_caps) {
	struct wlr_output_group *group = group_from_output(output);

	// TODO: intersect primary formats from all children
	return group->main_output->impl->get_primary_formats(group->main_output, buffer_caps);
}

static const struct wlr_output_impl output_impl = {
	.destroy = group_destroy,
	.commit = group_commit,
	.get_primary_formats = group_get_primary_formats,
};

static void group_handle_main_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_output_group *group =
		wl_container_of(listener, group, main_output_destroy);
	wlr_output_destroy(&group->base);
}

struct wlr_output_group *wlr_output_group_create(struct wlr_output *main_output) {
	struct wlr_output_group *group = calloc(1, sizeof(*group));
	if (group == NULL) {
		return NULL;
	}

	wlr_output_init(&group->base, main_output->backend,
		&output_impl, main_output->display);

	wl_list_init(&group->children);

	group->main_output_destroy.notify = group_handle_main_output_destroy;
	wl_signal_add(&main_output->events.destroy, &group->main_output_destroy);

	memcpy(&group->base.name, &main_output->name, sizeof(group->base.name));
	wlr_output_set_description(&group->base, main_output->description);
	memcpy(&group->base.make, &main_output->make, sizeof(group->base.make));
	memcpy(&group->base.model, &main_output->model, sizeof(group->base.model));
	memcpy(&group->base.serial, &main_output->serial, sizeof(group->base.serial));
	group->base.phys_width = main_output->phys_width;
	group->base.phys_height = main_output->phys_height;
	group->base.modes = main_output->modes;
	group->base.current_mode = main_output->current_mode;
	group->base.width = main_output->width;
	group->base.height = main_output->height;
	group->base.refresh = main_output->refresh;
	group->base.enabled = main_output->enabled;
	group->base.scale = main_output->scale;
	group->base.subpixel = main_output->subpixel;
	group->base.transform = main_output->transform;
	group->base.adaptive_sync_status = main_output->adaptive_sync_status;

	// TODO: listen to main output events and pass them through

	return group;
}

static void child_handle_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_output_group_child *child =
		wl_container_of(listener, child, output_destroy);
	wlr_output_group_child_destroy(child);
}

struct wlr_output_group_child *wlr_output_group_add(
		struct wlr_output_group *group, struct wlr_output *output) {
	struct wlr_output_group_child *child;
	wl_list_for_each(child, &group->children, link) {
		if (child->output == output) {
			return child;
		}
	}

	child = calloc(1, sizeof(*child));
	if (child == NULL) {
		return NULL;
	}

	child->output = output;
	wl_list_insert(&group->children, &child->link);

	child->output_destroy.notify = child_handle_output_destroy;
	wl_signal_add(&output->events.destroy, &child->output_destroy);

	return child;
}

void wlr_output_group_child_destroy(struct wlr_output_group_child *child) {
	wl_list_remove(&child->output_destroy.link);
	wl_list_remove(&child->link);
	free(child);
}
