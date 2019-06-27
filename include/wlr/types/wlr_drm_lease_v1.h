/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_DRM_LEASE_V1_H
#define WLR_TYPES_WLR_DRM_LEASE_V1_H

#include <wayland-server.h>
#include <wlr/backend.h>

struct wlr_drm_backend;

struct wlr_drm_lease_manager_v1 {
	struct wl_list resources; // wl_resource_get_link
	struct wl_global *global;
	struct wlr_drm_backend *backend;

	struct wl_list connectors; // wlr_drm_lease_connector_v1::link
	struct wl_list leases; // wl_resource_get_link
	struct wl_list lease_requests; // wl_resource_get_link

	struct {
		/**
		 * Upon receiving this signal, call
		 * wlr_drm_lease_manager_v1_grant_lease_request to grant a lease of the
		 * requested DRM resources, or
		 * wlr_drm_lease_manager_v1_reject_lease_request to reject the request.
		 */
		struct wl_signal lease_requested; // wlr_drm_lease_request_v1
	} events;

	void *data;
};

struct wlr_drm_connector;
struct wlr_drm_lease_v1;

/** Represents a connector which is available for leasing, and may be leased */
struct wlr_drm_lease_connector_v1 {
	struct wlr_output *output;
	struct wlr_drm_connector *drm_connector;
	struct wl_list resources; // wl_resource_get_link

	/** NULL if no client is currently leasing this connector */
	struct wlr_drm_lease_v1 *active_lease;

	struct wl_list link; // wlr_drm_lease_manager_v1::connectors
};

/** Represents a connector which has been added to a lease or lease request */
struct wlr_drm_connector_lease_v1 {
	struct wlr_drm_lease_manager_v1 *manager;
	struct wlr_drm_lease_connector_v1 *connector;
	struct wl_list link; // wlr_drm_lease_request_v1::connectors
};

/** Represents a pending or submitted lease request */
struct wlr_drm_lease_request_v1 {
	struct wlr_drm_lease_manager_v1 *manager;
	struct wl_resource *resource; // wlr_drm_manager_v1::lease_requests
	struct wl_list connectors; // wlr_drm_connector_lease_v1::link
	bool invalid;

	/** NULL until the lease is submitted */
	struct wlr_drm_lease_v1 *lease;
};

/** Represents an active or previously active lease */
struct wlr_drm_lease_v1 {
	struct wlr_drm_lease_manager_v1 *manager;
	struct wl_resource *resource; // wlr_drm_manager_v1::leases
	struct wl_list connectors; // wlr_drm_connector_lease_v1::link
	uint32_t lessee_id;

	struct {
		/**
		 * Upon receiving this signal, it is safe to re-use the leased
		 * resources. After the signal is processed, the backend will re-signal
		 * new_output events for each leased output.
		 *
		 * The lifetime of the lease reference after the signal handler returns
		 * is not defined.
		 */
		struct wl_signal revoked; // wlr_drm_lease_v1
	} events;

	void *data;
};

/**
 * Creates a DRM lease manager. The backend supplied must be a DRM backend, or a
 * multi-backend with a single DRM backend within. If the supplied backend is
 * not suitable, NULL is returned.
 */
struct wlr_drm_lease_manager_v1 *wlr_drm_lease_manager_v1_create(
		struct wl_display *display, struct wlr_backend *backend);

/**
 * Offers a wlr_output for lease. The offered output must be owned by the DRM
 * backend associated with this lease manager.
 */
void wlr_drm_lease_manager_v1_offer_output(
		struct wlr_drm_lease_manager_v1 *manager, struct wlr_output *output);

/**
 * Withdraws a previously offered output for lease. It is a programming error to
 * call this function when there are any active leases for this output.
 */
void wlr_drm_lease_manager_v1_widthraw_output(
		struct wlr_drm_lease_manager_v1 *manager, struct wlr_output *output);

/**
 * Grants a client's lease request. The lease manager will then provision the
 * DRM lease and transfer the file descriptor to the client. After calling this,
 * each wlr_output leased is destroyed, and will be re-issued through
 * wlr_backend.events.new_outputs when the lease is revoked.
 *
 * This will return NULL without leasing any resources if the lease is invalid;
 * this can happen for example if two clients request the same resources and an
 * attempt to grant both leases is made.
 */
struct wlr_drm_lease_v1 *wlr_drm_lease_manager_v1_grant_lease_request(
		struct wlr_drm_lease_manager_v1 *manager,
		struct wlr_drm_lease_request_v1 *request);

/**
 * Rejects a client's lease request.
 */
void wlr_drm_lease_manager_v1_reject_lease_request(
		struct wlr_drm_lease_manager_v1 *manager,
		struct wlr_drm_lease_request_v1 *request);

/**
 * Revokes a client's lease request. You may resume use of any of the outputs
 * leased after making this call.
 */
void wlr_drm_lease_manager_v1_revoke_lease(
		struct wlr_drm_lease_manager_v1 *manager,
		struct wlr_drm_lease_v1 *lease);

#endif
