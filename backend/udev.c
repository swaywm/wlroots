#define _POSIX_C_SOURCE 200809L
#include <libudev.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wayland-server.h>
#include <wlr/session.h>
#include <wlr/util/log.h>
#include "backend/udev.h"

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

		udev_device_unref(dev);

		// We've found the primary GPU
		if (is_boot_vga) {
			break;
		}
	}

	udev_enumerate_unref(en);

	return fd;
}

static int udev_event(int fd, uint32_t mask, void *data) {
	struct wlr_udev *udev = data;

	struct udev_device *dev = udev_monitor_receive_device(udev->mon);
	if (!dev) {
		return 1;
	}

	const char *action = udev_device_get_action(dev);

	wlr_log(L_DEBUG, "udev event for %s (%s)",
			udev_device_get_sysname(dev), action);

	if (!action || strcmp(action, "change") != 0) {
		goto out;
	}

	dev_t devnum = udev_device_get_devnum(dev);
	struct wlr_udev_dev *signal;

	wl_list_for_each(signal, &udev->devices, link) {
		if (signal->dev == devnum) {
			wl_signal_emit(&signal->invalidate, udev);
			break;
		}
	}

out:
	udev_device_unref(dev);
	return 1;
}

struct wlr_udev *wlr_udev_create(struct wl_display *display) {
	struct wlr_udev *udev = calloc(sizeof(struct wlr_udev), 1);
	if (!udev) {
		return NULL;
	}
	udev->udev = udev_new();
	if (!udev->udev) {
		wlr_log(L_ERROR, "Failed to create udev context");
		goto error;
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
	
	wl_list_init(&udev->devices);

	wlr_log(L_DEBUG, "Successfully initialized udev");
	return udev;

error_mon:
	udev_monitor_unref(udev->mon);
error_udev:
	udev_unref(udev->udev);
error:
	free(udev);
	return NULL;
}

void wlr_udev_destroy(struct wlr_udev *udev) {
	if (!udev) {
		return;
	}

	struct wlr_udev_dev *dev, *tmp;
	wl_list_for_each_safe(dev, tmp, &udev->devices, link) {
		free(dev);
	}

	wl_event_source_remove(udev->event);
	udev_monitor_unref(udev->mon);
	udev_unref(udev->udev);
}

bool wlr_udev_signal_add(struct wlr_udev *udev, dev_t dev, struct wl_listener *listener) {
	struct wlr_udev_dev *device = malloc(sizeof(*device));
	if (!device) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return false;
	}

	device->dev = dev;
	wl_signal_init(&device->invalidate);
	wl_signal_add(&device->invalidate, listener);
	wl_list_insert(&udev->devices, &device->link);

	return true;
}

void wlr_udev_signal_remove(struct wlr_udev *udev, struct wl_listener *listener) {
	if (!udev || !listener) {
		return;
	}

	struct wlr_udev_dev *dev, *tmp;
	wl_list_for_each_safe(dev, tmp, &udev->devices, link) {
		// The signal should only have a single listener
		if (wl_signal_get(&dev->invalidate, listener->notify) != NULL) {
			wl_list_remove(&dev->link);
			free(dev);
			return;
		}
	}
}
