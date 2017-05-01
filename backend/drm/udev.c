#define _POSIX_C_SOURCE 200809L

#include <libudev.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backend/drm/backend.h"
#include "backend/drm/udev.h"
#include "backend/drm/session.h"
#include "backend/drm/drm.h"

static bool device_is_kms(struct wlr_udev *udev,
		struct wlr_session *session,
		struct udev_device *dev,
		int *fd_out)
{
	const char *path = udev_device_get_devnode(dev);
	int fd;

	if (!path)
		return false;

	fd = wlr_session_take_device(session, path, NULL);
	if (fd < 0)
		return false;

	drmModeRes *res = drmModeGetResources(fd);
	if (!res)
		goto out_fd;

	if (res->count_crtcs <= 0 || res->count_connectors <= 0 ||
	    res->count_encoders <= 0)
		goto out_res;

	if (*fd_out >= 0) {
		wlr_session_release_device(session, *fd_out);
		free(udev->drm_path);
	}

	*fd_out = fd;
	udev->drm_path = strdup(path);

	drmModeFreeResources(res);
	return true;

out_res:
	drmModeFreeResources(res);
out_fd:
	wlr_session_release_device(session, fd);
	return false;
}

int wlr_udev_find_gpu(struct wlr_udev *udev, struct wlr_session *session)
{
	int fd = -1;

	struct udev_enumerate *en = udev_enumerate_new(udev->udev);
	if (!en)
		return -1;

	udev_enumerate_add_match_subsystem(en, "drm");
	udev_enumerate_add_match_sysname(en, "card[0-9]*");

	udev_enumerate_scan_devices(en);
	struct udev_list_entry *entry;
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(en)) {
		bool is_boot_vga = false;

		const char *path = udev_list_entry_get_name(entry);
		struct udev_device *dev = udev_device_new_from_syspath(udev->udev, path);
		if (!dev)
			continue;

		const char *seat = udev_device_get_property_value(dev, "ID_SEAT");
		if (!seat)
			seat = "seat0";
		if (strcmp(session->seat, seat) != 0) {
			udev_device_unref(dev);
			continue;
		}

		struct udev_device *pci =
			udev_device_get_parent_with_subsystem_devtype(dev,
								      "pci", NULL);

		if (pci) {
			const char *id = udev_device_get_sysattr_value(pci, "boot_vga");
			if (id && strcmp(id, "1") == 0)
				is_boot_vga = true;
			udev_device_unref(pci);
		}

		if (!is_boot_vga && fd >= 0) {
			udev_device_unref(dev);
			continue;
		}

		if (!device_is_kms(udev, session, dev, &fd)) {
			udev_device_unref(dev);
			continue;
		}

		if (is_boot_vga) {
			break;
		}
	}

	udev_enumerate_unref(en);

	return fd;
}

bool wlr_udev_init(struct wlr_udev *udev)
{
	udev->udev = udev_new();
	if (!udev->udev)
		return false;

	udev->mon = udev_monitor_new_from_netlink(udev->udev, "udev");
	if (!udev->mon) {
		udev_unref(udev->udev);
		return false;
	}

	udev_monitor_filter_add_match_subsystem_devtype(udev->mon, "drm", NULL);
	udev_monitor_enable_receiving(udev->mon);

	udev->mon_fd = udev_monitor_get_fd(udev->mon);
	udev->drm_path = NULL;

	return true;
}

void wlr_udev_free(struct wlr_udev *udev)

{
	if (!udev)
		return;

	udev_monitor_unref(udev->mon);
	udev_unref(udev->udev);
	free(udev->drm_path);
}

void wlr_udev_event(struct wlr_drm_backend *backend)
{
	struct wlr_udev *udev = &backend->udev;

	struct udev_device *dev = udev_monitor_receive_device(udev->mon);
	if (!dev)
		return;

	const char *path = udev_device_get_devnode(dev);
	if (!path || strcmp(path, udev->drm_path) != 0)
		goto out;

	const char *action = udev_device_get_action(dev);
	if (!action || strcmp(action, "change") != 0)
		goto out;

	wlr_drm_scan_connectors(backend);

out:
	udev_device_unref(dev);
}
