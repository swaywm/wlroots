#include <libudev.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "otd.h"
#include "udev.h"
#include "session.h"
#include "drm.h"

static bool device_is_kms(struct otd *otd, struct udev_device *dev)
{
	const char *path = udev_device_get_devnode(dev);
	int fd;

	if (!path)
		return false;

	fd = take_device(otd, path, &otd->paused);
	if (fd < 0)
		return false;

	drmModeRes *res = drmModeGetResources(fd);
	if (!res)
		goto out_fd;

	if (res->count_crtcs <= 0 || res->count_connectors <= 0 ||
	    res->count_encoders <= 0)
		goto out_res;

	if (otd->fd >= 0) {
		release_device(otd, otd->fd);
		free(otd->drm_path);
	}

	otd->fd = fd;
	otd->drm_path = strdup(path);

	drmModeFreeResources(res);
	return true;

out_res:
	drmModeFreeResources(res);
out_fd:
	release_device(otd, fd);
	return false;
}

void otd_udev_find_gpu(struct otd *otd)
{
	struct udev *udev = otd->udev;
	otd->fd = -1;

	struct udev_enumerate *en = udev_enumerate_new(udev);
	if (!en)
		return;

	udev_enumerate_add_match_subsystem(en, "drm");
	udev_enumerate_add_match_sysname(en, "card[0-9]*");

	udev_enumerate_scan_devices(en);
	struct udev_list_entry *entry;
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(en)) {
		bool is_boot_vga = false;

		const char *path = udev_list_entry_get_name(entry);
		struct udev_device *dev = udev_device_new_from_syspath(udev, path);
		if (!dev)
			continue;

		const char *seat = udev_device_get_property_value(dev, "ID_SEAT");
		if (!seat)
			seat = "seat0";
		if (strcmp(otd->session.seat, seat) != 0) {
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

		if (!is_boot_vga && otd->fd >= 0) {
			udev_device_unref(dev);
			continue;
		}

		if (!device_is_kms(otd, dev)) {
			udev_device_unref(dev);
			continue;
		}

		if (is_boot_vga) {
			break;
		}
	}

	udev_enumerate_unref(en);
}

bool otd_udev_start(struct otd *otd)
{
	otd->udev = udev_new();
	if (!otd->udev)
		return false;

	otd->mon = udev_monitor_new_from_netlink(otd->udev, "udev");
	if (!otd->mon) {
		udev_unref(otd->udev);
		return false;
	}

	udev_monitor_filter_add_match_subsystem_devtype(otd->mon, "drm", NULL);
	udev_monitor_enable_receiving(otd->mon);

	otd->udev_fd = udev_monitor_get_fd(otd->mon);

	return true;
}

void otd_udev_finish(struct otd *otd)
{
	if (!otd)
		return;

	udev_monitor_unref(otd->mon);
	udev_unref(otd->udev);
}

void otd_udev_event(struct otd *otd)
{
	struct udev_device *dev = udev_monitor_receive_device(otd->mon);
	if (!dev)
		return;

	const char *path = udev_device_get_devnode(dev);
	if (!path || strcmp(path, otd->drm_path) != 0)
		goto out;

	const char *action = udev_device_get_action(dev);
	if (!action || strcmp(action, "change") != 0)
		goto out;

	scan_connectors(otd);

out:
	udev_device_unref(dev);
}
