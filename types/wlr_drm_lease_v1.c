#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_drm_lease_v1.h>
#include <wlr/util/log.h>
#include <wayland-util.h>
#include <wayland-server.h>
#include <xf86drmMode.h>
#include "backend/drm/drm.h"
#include "drm-lease-unstable-v1-protocol.h"
#include "util/shm.h"
#include "util/signal.h"

static struct zwp_drm_lease_device_v1_interface lease_device_impl;
static struct zwp_drm_lease_connector_v1_interface lease_connector_impl;
static struct zwp_drm_lease_request_v1_interface lease_request_impl;
static struct zwp_drm_lease_v1_interface lease_impl;

static void drm_lease_connector_v1_send_to_client(
		struct wlr_drm_lease_connector_v1 *connector,
		struct wl_client *wl_client, struct wl_resource *device);

static struct wlr_drm_lease_device_v1 *wlr_drm_lease_device_v1_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
				&zwp_drm_lease_device_v1_interface, &lease_device_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_drm_lease_request_v1 *wlr_drm_lease_request_v1_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
				&zwp_drm_lease_request_v1_interface, &lease_request_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_drm_lease_connector_v1 *
wlr_drm_lease_connector_v1_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
				&zwp_drm_lease_connector_v1_interface, &lease_connector_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_drm_lease_v1 *wlr_drm_lease_v1_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
				&zwp_drm_lease_v1_interface, &lease_impl));
	return wl_resource_get_user_data(resource);
}

static bool drm_lease_request_v1_validate(
		struct wlr_drm_lease_request_v1 *req) {
	if (req->invalid) {
		return false;
	}
	/* Don't lease connectors which are already leased */
	struct wlr_drm_connector_lease_v1 *connector;
	wl_list_for_each(connector, &req->connectors, link) {
		if (connector->connector->active_lease) {
			return false;
		}
	}
	return true;
}

static void lease_terminated_by_drm(
		struct wlr_drm_connector *conn, void *data) {
	wlr_log(WLR_DEBUG, "Lease terminated by DRM");
	struct wlr_drm_lease_v1 *lease = data;
	lease->lessee_id = 0;
	wlr_drm_lease_device_v1_revoke_lease(lease->device, lease);
}

struct wlr_drm_lease_v1 *wlr_drm_lease_device_v1_grant_lease_request(
		struct wlr_drm_lease_device_v1 *device,
		struct wlr_drm_lease_request_v1 *request) {
	assert(device && request);
	assert(request->lease);

	struct wlr_drm_lease_v1 *lease = request->lease;
	if (!drm_lease_request_v1_validate(request)) {
		zwp_drm_lease_v1_send_finished(lease->resource);
		return NULL;
	}

	int nconns = 0;

	/** Adopt the connector leases from the lease request */
	struct wlr_drm_connector_lease_v1 *conn, *temp;
	wl_list_for_each_safe(conn, temp, &request->connectors, link) {
		wl_list_remove(&conn->link);
		wl_list_init(&conn->link);
		wl_list_insert(&lease->connectors, &conn->link);
		++nconns;
	}

	struct wlr_drm_connector *conns[nconns + 1];
	int i = 0;
	wl_list_for_each(conn, &lease->connectors, link) {
		conns[i] = conn->connector->drm_connector;
		++i;
	}

	int fd = drm_create_lease(device->backend,
			conns, nconns, &lease->lessee_id,
			lease_terminated_by_drm, lease);

	if (fd < 0) {
		wlr_log_errno(WLR_ERROR, "drm_create_lease failed");
		zwp_drm_lease_v1_send_finished(lease->resource);
		return NULL;
	}

	wl_list_for_each(conn, &lease->connectors, link) {
		struct wlr_drm_lease_connector_v1 *conn_lease =
			conn->connector;
		conn_lease->active_lease = lease;

		struct wl_resource *wl_resource, *temp;
		wl_resource_for_each_safe(wl_resource, temp, &conn_lease->resources) {
			zwp_drm_lease_connector_v1_send_withdrawn(wl_resource);
			wl_resource_set_user_data(wl_resource, NULL);
			wl_list_remove(wl_resource_get_link(wl_resource));
			wl_list_init(wl_resource_get_link(wl_resource));
		}
	}

	zwp_drm_lease_v1_send_lease_fd(lease->resource, fd);
	close(fd);
	return lease;
}

void wlr_drm_lease_device_v1_reject_lease_request(
		struct wlr_drm_lease_device_v1 *device,
		struct wlr_drm_lease_request_v1 *request) {
	assert(device && request);
	assert(request->lease);
	zwp_drm_lease_v1_send_finished(request->lease->resource);
	request->invalid = true;
}

void wlr_drm_lease_device_v1_revoke_lease(
		struct wlr_drm_lease_device_v1 *device,
		struct wlr_drm_lease_v1 *lease) {
	assert(device && lease);
	if (lease->resource != NULL) {
		zwp_drm_lease_v1_send_finished(lease->resource);
	}
	if (lease->lessee_id != 0) {
		if (drm_terminate_lease(device->backend, lease->lessee_id) < 0) {
			wlr_log_errno(WLR_DEBUG, "drm_terminate_lease");
		}
	}
	struct wlr_drm_connector_lease_v1 *conn;
	wl_list_for_each(conn, &lease->connectors, link) {
		struct wlr_drm_lease_connector_v1 *conn_lease =
			conn->connector;
		conn_lease->active_lease = NULL;

		struct wl_resource *wl_resource;
		wl_resource_for_each(wl_resource, &device->resources) {
			struct wl_client *wl_client = wl_resource_get_client(wl_resource);
			drm_lease_connector_v1_send_to_client(
					conn_lease, wl_client, wl_resource);
		}
	}
	wlr_signal_emit_safe(&lease->events.revoked, lease);
}

static void drm_lease_v1_destroy(struct wlr_drm_lease_v1 *lease) {
	wlr_drm_lease_device_v1_revoke_lease(lease->device, lease);
	free(lease);
}

static void drm_lease_v1_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_drm_lease_v1 *lease = wlr_drm_lease_v1_from_resource(resource);
	wl_list_remove(wl_resource_get_link(resource));
	wl_list_init(wl_resource_get_link(resource));
	lease->resource = NULL;
	drm_lease_v1_destroy(lease);
}

static void drm_lease_v1_handle_destroy(
		struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_drm_lease_v1_interface lease_impl = {
	.destroy = drm_lease_v1_handle_destroy,
};

static void drm_lease_request_v1_destroy(struct wlr_drm_lease_request_v1 *req) {
	if (!req) {
		return;
	}
	struct wlr_drm_connector_lease_v1 *conn, *temp;
	wl_list_for_each_safe(conn, temp, &req->connectors, link) {
		wl_list_remove(&conn->link);
		wl_list_init(&conn->link);
		free(conn);
	}
	free(req);
}

static void drm_lease_request_v1_handle_resource_destroy(
		struct wl_resource *resource) {
	struct wlr_drm_lease_request_v1 *req =
		wlr_drm_lease_request_v1_from_resource(resource);
	drm_lease_request_v1_destroy(req);
	wl_list_remove(wl_resource_get_link(resource));
	wl_list_init(wl_resource_get_link(resource));
}

static void drm_lease_request_v1_handle_request_connector(
		struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *connector) {
	struct wlr_drm_lease_request_v1 *request =
		wlr_drm_lease_request_v1_from_resource(resource);
	struct wlr_drm_lease_connector_v1 *conn =
		wlr_drm_lease_connector_v1_from_resource(connector);

	if (conn == NULL) {
		/* This connector offer has been withdrawn */
		request->invalid = true;
		return;
	}

	struct wlr_drm_connector_lease_v1 *lease =
		calloc(1, sizeof(struct wlr_drm_connector_lease_v1));
	if (!lease) {
		wl_client_post_no_memory(client);
		return;
	}

	lease->connector = conn;
	wl_list_insert(&request->connectors, &lease->link);
}

static void drm_lease_request_v1_handle_submit(
		struct wl_client *client, struct wl_resource *resource, uint32_t id) {
	struct wlr_drm_lease_request_v1 *lease_request =
		wlr_drm_lease_request_v1_from_resource(resource);

	struct wlr_drm_lease_v1 *lease = calloc(1, sizeof(struct wlr_drm_lease_v1));
	if (!lease) {
		wl_resource_post_no_memory(resource);
		return;
	}

	struct wl_resource *wl_resource = wl_resource_create(
			client, &zwp_drm_lease_v1_interface, 1, id);
	if (!wl_resource) {
		free(lease);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_signal_init(&lease->events.revoked);
	wl_list_init(&lease->connectors);
	lease->device = lease_request->device;
	lease->resource = wl_resource;
	lease_request->lease = lease;
	wl_list_insert(&lease->device->leases, wl_resource_get_link(wl_resource));

	wl_resource_set_implementation(wl_resource, &lease_impl,
			lease, drm_lease_v1_handle_resource_destroy);

	if (!drm_lease_request_v1_validate(lease_request)) {
		/* Pre-emptively reject invalid lease requests */
		zwp_drm_lease_v1_send_finished(lease->resource);
	} else {
		wlr_signal_emit_safe(
				&lease_request->device->events.lease_requested,
				lease_request);
	}
	wl_resource_destroy(resource);
}

static struct zwp_drm_lease_request_v1_interface lease_request_impl = {
	.request_connector = drm_lease_request_v1_handle_request_connector,
	.submit = drm_lease_request_v1_handle_submit,
};

static void drm_lease_device_v1_handle_resource_destroy(
		struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
	wl_list_init(wl_resource_get_link(resource));
}

static void drm_lease_device_v1_handle_stop(
		struct wl_client *client, struct wl_resource *resource) {
	zwp_drm_lease_device_v1_send_finished(resource);
	wl_resource_destroy(resource);
}

static void drm_lease_device_v1_handle_create_lease_request(
		struct wl_client *client, struct wl_resource *resource, uint32_t id) {
	struct wlr_drm_lease_device_v1 *device =
		wlr_drm_lease_device_v1_from_resource(resource);

	struct wlr_drm_lease_request_v1 *req =
		calloc(1, sizeof(struct wlr_drm_lease_request_v1));
	if (!req) {
		wl_resource_post_no_memory(resource);
		return;
	}

	struct wl_resource *wl_resource = wl_resource_create(client,
		&zwp_drm_lease_request_v1_interface, 1, id);
	if (!wl_resource) {
		wl_resource_post_no_memory(resource);
		free(req);
		return;
	}

	req->device = device;
	req->resource = wl_resource;
	wl_list_init(&req->connectors);

	wl_resource_set_implementation(wl_resource, &lease_request_impl,
		req, drm_lease_request_v1_handle_resource_destroy);

	wl_list_insert(&device->lease_requests, wl_resource_get_link(wl_resource));
}

static struct zwp_drm_lease_device_v1_interface lease_device_impl = {
	.stop = drm_lease_device_v1_handle_stop,
	.create_lease_request = drm_lease_device_v1_handle_create_lease_request,
};

static void drm_connector_v1_handle_resource_destroy(
		struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
	wl_list_init(wl_resource_get_link(resource));
}

static void drm_connector_v1_handle_destroy(
		struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_drm_lease_connector_v1_interface lease_connector_impl = {
	.destroy = drm_connector_v1_handle_destroy,
};

static void drm_lease_connector_v1_send_to_client(
		struct wlr_drm_lease_connector_v1 *connector,
		struct wl_client *wl_client, struct wl_resource *device) {
	if (connector->active_lease) {
		return;
	}

	struct wl_resource *wl_resource = wl_resource_create(wl_client,
			&zwp_drm_lease_connector_v1_interface, 1, 0);
	wl_resource_set_implementation(wl_resource, &lease_connector_impl,
			connector, drm_connector_v1_handle_resource_destroy);
	zwp_drm_lease_device_v1_send_connector(device, wl_resource);

	struct wlr_output *output = connector->output;
	zwp_drm_lease_connector_v1_send_name(wl_resource, output->name);

	char description[128];
	snprintf(description, sizeof(description), "%s %s %s (%s)",
		output->make, output->model, output->serial, output->name);
	zwp_drm_lease_connector_v1_send_description(wl_resource, description);

	zwp_drm_lease_connector_v1_send_connector_id(
			wl_resource, connector->drm_connector->id);

	wl_list_insert(&connector->resources, wl_resource_get_link(wl_resource));
}

static void lease_device_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_drm_lease_device_v1 *lease_device = data;

	struct wl_resource *wl_resource  = wl_resource_create(wl_client,
		&zwp_drm_lease_device_v1_interface, version, id);

	if (!wl_resource) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_resource_set_implementation(wl_resource, &lease_device_impl,
		lease_device, drm_lease_device_v1_handle_resource_destroy);

	char *path = drmGetDeviceNameFromFd2(lease_device->backend->fd);
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		wlr_log_errno(WLR_ERROR, "Unable to clone DRM fd for leasing.\n");
		free(path);
	} else {
		if (drmIsMaster(fd)) {
			drmDropMaster(fd);
		}
		assert(!drmIsMaster(fd) && "Won't send master fd to client");
		zwp_drm_lease_device_v1_send_drm_fd(wl_resource, fd);
		free(path);
		close(fd);
	}

	wl_list_insert(&lease_device->resources,
			wl_resource_get_link(wl_resource));

	struct wlr_drm_lease_connector_v1 *connector;
	wl_list_for_each(connector, &lease_device->connectors, link) {
		drm_lease_connector_v1_send_to_client(
				connector, wl_client, wl_resource);
	}
}

void wlr_drm_lease_device_v1_offer_output(
		struct wlr_drm_lease_device_v1 *device, struct wlr_output *output) {
	assert(device && output);
	assert(wlr_output_is_drm(output));
	struct wlr_drm_connector *drm_connector =
		(struct wlr_drm_connector *)output;
	/*
	 * When the compositor grants a lease, we "destroy" all of the outputs on
	 * that lease. When the lease ends, the outputs re-appear. However, the
	 * underlying DRM connector remains the same. If the compositor offers
	 * outputs based on some criteria, then sees the output re-appear with the
	 * same critera, this code allows it to safely re-offer outputs which are
	 * backed by DRM connectors it has leased in the past.
	 */
	struct wlr_drm_lease_connector_v1 *connector;
	wl_list_for_each(connector, &device->connectors, link) {
		if (connector->drm_connector == drm_connector) {
			return;
		}
	}

	connector = calloc(1, sizeof(struct wlr_drm_lease_connector_v1));
	connector->drm_connector = drm_connector;
	connector->output = &drm_connector->output;
	wl_list_init(&connector->resources);
	wl_list_insert(&device->connectors, &connector->link);

	struct wl_resource *resource;
	wl_resource_for_each(resource, &device->resources) {
		drm_lease_connector_v1_send_to_client(
				connector, wl_resource_get_client(resource), resource);
	}
}

void wlr_drm_lease_device_v1_widthraw_output(
		struct wlr_drm_lease_device_v1 *device, struct wlr_output *output) {
	struct wlr_drm_lease_connector_v1 *connector = NULL, *_connector;
	wl_list_for_each(_connector, &device->connectors, link) {
		if (_connector->output == output) {
			connector = _connector;
			break;
		}
	}
	if (!connector) {
		return;
	}
	assert(connector->active_lease == NULL && "Cannot withdraw a leased output");

	struct wl_resource *wl_resource, *temp;
	wl_resource_for_each_safe(wl_resource, temp, &connector->resources) {
		zwp_drm_lease_connector_v1_send_withdrawn(wl_resource);
		wl_resource_set_user_data(wl_resource, NULL);
		wl_list_remove(wl_resource_get_link(wl_resource));
		wl_list_init(wl_resource_get_link(wl_resource));
	}

	wl_resource_for_each(wl_resource, &device->lease_requests) {
		struct wlr_drm_lease_request_v1 *request =
			wlr_drm_lease_request_v1_from_resource(wl_resource);
		request->invalid = true;
	}

	wl_list_remove(&connector->link);
	wl_list_init(&connector->link);
	free(connector);
}

static void multi_backend_cb(struct wlr_backend *backend, void *data) {
	struct wlr_backend **ptr = data;
	if (wlr_backend_is_drm(backend)) {
		*ptr = backend;
	}
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_drm_lease_device_v1 *device =
		wl_container_of(listener, device, display_destroy);
	struct wl_resource *resource;
	struct wl_resource *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &device->resources) {
		wl_resource_destroy(resource);
	}

	wl_resource_for_each_safe(resource, tmp_resource,
			&device->lease_requests) {
		wl_resource_destroy(resource);
	}

	wl_resource_for_each_safe(resource, tmp_resource, &device->leases) {
		struct wlr_drm_lease_v1 *lease =
			wlr_drm_lease_v1_from_resource(resource);
		wlr_drm_lease_device_v1_revoke_lease(device, lease);
	}

	struct wlr_drm_lease_connector_v1 *connector, *tmp_connector;
	wl_list_for_each_safe(connector, tmp_connector,
			&device->connectors, link) {
		wl_list_remove(&connector->link);
		wl_list_init(&connector->link);
		free(connector);
	}

	free(device);
}

struct wlr_drm_lease_device_v1 *wlr_drm_lease_device_v1_create(
		struct wl_display *display, struct wlr_backend *backend) {
	assert(display);

	if (!wlr_backend_is_drm(backend) && wlr_backend_is_multi(backend)) {
		wlr_multi_for_each_backend(backend, multi_backend_cb, &backend);
		if (!wlr_backend_is_drm(backend)) {
			return NULL;
		}
	} else {
		return NULL;
	}

	struct wlr_drm_lease_device_v1 *lease_device =
		calloc(1, sizeof(struct wlr_drm_lease_device_v1));

	if (!lease_device) {
		return NULL;
	}

	lease_device->backend = get_drm_backend_from_backend(backend);
	wl_list_init(&lease_device->resources);
	wl_list_init(&lease_device->connectors);
	wl_list_init(&lease_device->lease_requests);
	wl_list_init(&lease_device->leases);

	wl_signal_init(&lease_device->events.lease_requested);

	lease_device->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &lease_device->display_destroy);

	lease_device->global = wl_global_create(display,
		&zwp_drm_lease_device_v1_interface, 1,
		lease_device, lease_device_bind);

	if (!lease_device->global) {
		free(lease_device);
		return NULL;
	}

	return lease_device;
}
