#define _POSIX_C_SOURCE 200809L

#include <libudev.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wayland-server.h>

#include <wlr/session.h>

#include "backend/drm/backend.h"
#include "backend/drm/udev.h"
#include "backend/drm/drm.h"
#include "common/log.h"

/* Tests if 'path' is KMS compatible by trying to open it.
 * It leaves the open device in *fd_out it it succeeds.
 */
static bool device_is_kms(struct wlr_session *restrict session,
	const char *restrict path, int *restrict fd_out) {

	int fd;

	if (!path) {
		return false;
	}

	fd = wlr_session_open_file(session, path);
	if (fd < 0) {
		return false;
	}

	drmModeRes *res = drmModeGetResources(fd);
	if (!res) {
		goto out_fd;
	}

	if (res->count_crtcs <= 0 || res->count_connectors <= 0 ||
		res->count_encoders <= 0) {

		goto out_res;
	}

	if (*fd_out >= 0) {
		wlr_session_close_file(session, *fd_out);
	}

	*fd_out = fd;

	drmModeFreeResources(res);
	return true;

out_res:
	drmModeFreeResources(res);
out_fd:
	wlr_session_close_file(session, fd);
	return false;
}

/* Tries to find the primary GPU by checking for the "boot_vga" attribute.
 * If it's not found, it returns the first valid GPU it finds.
 */
int wlr_udev_find_gpu(struct wlr_udev *udev, struct wlr_session *session) {
	struct udev_enumerate *en = udev_enumerate_new(udev->udev);
	if (!en) {
		wlr_log(L_ERROR, "Failed to create udev enumeration");
		return -1;
	}

	udev_enumerate_add_match_subsystem(en, "drm");
	udev_enumerate_add_match_sysname(en, "card[0-9]*");
	udev_enumerate_scan_devices(en);

	struct udev_list_entry *entry;
	int fd = -1;
	char *drm_path = NULL;

	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(en)) {
		bool is_boot_vga = false;

		const char *path = udev_list_entry_get_name(entry);
		struct udev_device *dev = udev_device_new_from_syspath(udev->udev, path);
		if (!dev) {
			continue;
		}

		/*
		const char *seat = udev_device_get_property_value(dev, "ID_SEAT");
		if (!seat)
			seat = "seat0";
		if (strcmp(session->seat, seat) != 0) {
			udev_device_unref(dev);
			continue;
		}
		*/

		// This is owned by 'dev', so we don't need to free it
		struct udev_device *pci =
			udev_device_get_parent_with_subsystem_devtype(dev, "pci", NULL);

		if (pci) {
			const char *id = udev_device_get_sysattr_value(pci, "boot_vga");
			if (id && strcmp(id, "1") == 0) {
				is_boot_vga = true;
			}
		}

		// We already have a valid GPU
		if (!is_boot_vga && fd >= 0) {
			udev_device_unref(dev);
			continue;
		}

		path = udev_device_get_devnode(dev);
		if (!device_is_kms(session, path, &fd)) {
			udev_device_unref(dev);
			continue;
		}

		free(drm_path);
		drm_path = strdup(path);

		udev_device_unref(dev);

		// We've found the primary GPU
		if (is_boot_vga) {
			break;
		}
	}

	udev_enumerate_unref(en);

	udev->drm_path = drm_path;
	return fd;
}

static int udev_event(int fd, uint32_t mask, void *data) {
	struct wlr_udev *udev = data;
	struct wlr_drm_backend *backend = wl_container_of(udev, backend, udev);

	struct udev_device *dev = udev_monitor_receive_device(udev->mon);
	if (!dev) {
		return 1;
	}

	const char *path = udev_device_get_devnode(dev);
	if (!path || strcmp(path, udev->drm_path) != 0) {
		goto out;
	}

	const char *action = udev_device_get_action(dev);
	if (!action || strcmp(action, "change") != 0) {
		goto out;
	}

	wlr_drm_scan_connectors(backend);

out:
	udev_device_unref(dev);
	return 1;
}

bool wlr_udev_init(struct wl_display *display, struct wlr_udev *udev) {
	udev->udev = udev_new();
	if (!udev->udev) {
		wlr_log(L_ERROR, "Failed to create udev context");
		return false;
	}

	udev->mon = udev_monitor_new_from_netlink(udev->udev, "udev");
	if (!udev->mon) {
		wlr_log(L_ERROR, "Failed to create udev monitor");
		goto error_udev;
	}

	udev_monitor_filter_add_match_subsystem_devtype(udev->mon, "drm", NULL);
	udev_monitor_enable_receiving(udev->mon);

	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);
	int fd = udev_monitor_get_fd(udev->mon);

	udev->event = wl_event_loop_add_fd(event_loop, fd, WL_EVENT_READABLE,
		udev_event, udev);
	if (!udev->event) {
		wlr_log(L_ERROR, "Failed to create udev event source");
		goto error_mon;
	}

	udev->drm_path = NULL;

	return true;

error_mon:
	udev_monitor_unref(udev->mon);
error_udev:
	udev_unref(udev->udev);
	return false;
}

void wlr_udev_free(struct wlr_udev *udev) {
	if (!udev) {
		return;
	}

	wl_event_source_remove(udev->event);

	udev_monitor_unref(udev->mon);
	udev_unref(udev->udev);
	free(udev->drm_path);
}
