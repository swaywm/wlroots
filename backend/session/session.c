#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <libudev.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend/session.h>
#include <wlr/backend/session/interface.h>
#include <wlr/config.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "backend/session/session.h"
#include "util/signal.h"

extern const struct session_impl session_libseat;
extern const struct session_impl session_logind;
extern const struct session_impl session_direct;
extern const struct session_impl session_noop;

static const struct session_impl *impls[] = {
#if WLR_HAS_LIBSEAT
	&session_libseat,
#endif
#if WLR_HAS_SYSTEMD || WLR_HAS_ELOGIND
	&session_logind,
#endif
	&session_direct,
	NULL,
};

static bool is_drm_card(const char *sysname) {
	const char prefix[] = "card";
	if (strncmp(sysname, prefix, strlen(prefix)) != 0) {
		return false;
	}
	for (size_t i = strlen(prefix); sysname[i] != '\0'; i++) {
		if (sysname[i] < '0' || sysname[i] > '9') {
			return false;
		}
	}
	return true;
}

static int udev_event(int fd, uint32_t mask, void *data) {
	struct wlr_session *session = data;

	struct udev_device *udev_dev = udev_monitor_receive_device(session->mon);
	if (!udev_dev) {
		return 1;
	}

	const char *sysname = udev_device_get_sysname(udev_dev);
	const char *devnode = udev_device_get_devnode(udev_dev);
	const char *action = udev_device_get_action(udev_dev);
	wlr_log(WLR_DEBUG, "udev event for %s (%s)", sysname, action);

	if (!is_drm_card(sysname) || !action || !devnode) {
		goto out;
	}

	const char *seat = udev_device_get_property_value(udev_dev, "ID_SEAT");
	if (!seat) {
		seat = "seat0";
	}
	if (session->seat[0] != '\0' && strcmp(session->seat, seat) != 0) {
		goto out;
	}

	if (strcmp(action, "add") == 0) {
		wlr_log(WLR_DEBUG, "DRM device %s added", sysname);
		struct wlr_session_add_event event = {
			.path = devnode,
		};
		wlr_signal_emit_safe(&session->events.add_drm_card, &event);
	} else if (strcmp(action, "change") == 0) {
		dev_t devnum = udev_device_get_devnum(udev_dev);
		struct wlr_device *dev;
		wl_list_for_each(dev, &session->devices, link) {
			if (dev->dev == devnum) {
				wlr_log(WLR_DEBUG, "DRM device %s changed", sysname);
				wlr_signal_emit_safe(&dev->events.change, NULL);
				break;
			}
		}
	}

out:
	udev_device_unref(udev_dev);
	return 1;
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_session *session =
		wl_container_of(listener, session, display_destroy);
	wlr_session_destroy(session);
}

void session_init(struct wlr_session *session) {
	wl_signal_init(&session->session_signal);
	wl_signal_init(&session->events.add_drm_card);
	wl_signal_init(&session->events.destroy);
	wl_list_init(&session->devices);
}

struct wlr_session *wlr_session_create(struct wl_display *disp) {
	struct wlr_session *session = NULL;

	const char *env_wlr_session = getenv("WLR_SESSION");
	if (env_wlr_session) {
		if (strcmp(env_wlr_session, "libseat") == 0) {
#if WLR_HAS_LIBSEAT
			session = session_libseat.create(disp);
#else
			wlr_log(WLR_ERROR, "wlroots is not compiled with libseat support");
#endif
		} else if (strcmp(env_wlr_session, "logind") == 0 ||
				strcmp(env_wlr_session, "systemd") == 0) {
#if WLR_HAS_SYSTEMD || WLR_HAS_ELOGIND
			session = session_logind.create(disp);
#else
			wlr_log(WLR_ERROR, "wlroots is not compiled with logind support");
#endif
		} else if (strcmp(env_wlr_session, "direct") == 0) {
			session = session_direct.create(disp);
		} else if (strcmp(env_wlr_session, "noop") == 0) {
			session = session_noop.create(disp);
		} else {
			wlr_log(WLR_ERROR, "Unsupported WLR_SESSION: %s",
				env_wlr_session);
		}
	} else {
		const struct session_impl **iter;
		for (iter = impls; !session && *iter; ++iter) {
			session = (*iter)->create(disp);
		}
	}

	if (!session) {
		wlr_log(WLR_ERROR, "Failed to load session backend");
		return NULL;
	}

	session->udev = udev_new();
	if (!session->udev) {
		wlr_log_errno(WLR_ERROR, "Failed to create udev context");
		goto error_session;
	}

	session->mon = udev_monitor_new_from_netlink(session->udev, "udev");
	if (!session->mon) {
		wlr_log_errno(WLR_ERROR, "Failed to create udev monitor");
		goto error_udev;
	}

	udev_monitor_filter_add_match_subsystem_devtype(session->mon, "drm", NULL);
	udev_monitor_enable_receiving(session->mon);

	struct wl_event_loop *event_loop = wl_display_get_event_loop(disp);
	int fd = udev_monitor_get_fd(session->mon);

	session->udev_event = wl_event_loop_add_fd(event_loop, fd,
		WL_EVENT_READABLE, udev_event, session);
	if (!session->udev_event) {
		wlr_log_errno(WLR_ERROR, "Failed to create udev event source");
		goto error_mon;
	}

	session->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(disp, &session->display_destroy);

	return session;

error_mon:
	udev_monitor_unref(session->mon);
error_udev:
	udev_unref(session->udev);
error_session:
	session->impl->destroy(session);
	return NULL;
}

void wlr_session_destroy(struct wlr_session *session) {
	if (!session) {
		return;
	}

	wlr_signal_emit_safe(&session->events.destroy, session);
	wl_list_remove(&session->display_destroy.link);

	wl_event_source_remove(session->udev_event);
	udev_monitor_unref(session->mon);
	udev_unref(session->udev);

	session->impl->destroy(session);
}

struct wlr_device *wlr_session_open_file(struct wlr_session *session,
		const char *path) {
	int fd = session->impl->open(session, path);
	if (fd < 0) {
		return NULL;
	}

	struct wlr_device *dev = malloc(sizeof(*dev));
	if (!dev) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto error;
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		wlr_log_errno(WLR_ERROR, "Stat failed");
		goto error;
	}

	dev->fd = fd;
	dev->dev = st.st_rdev;
	wl_signal_init(&dev->events.change);
	wl_list_insert(&session->devices, &dev->link);

	return dev;

error:
	free(dev);
	close(fd);
	return NULL;
}

void wlr_session_close_file(struct wlr_session *session,
		struct wlr_device *dev) {
	session->impl->close(session, dev->fd);
	wl_list_remove(&dev->link);
	free(dev);
}

bool wlr_session_change_vt(struct wlr_session *session, unsigned vt) {
	if (!session) {
		return false;
	}

	return session->impl->change_vt(session, vt);
}

/* Tests if 'path' is KMS compatible by trying to open it.
 * It leaves the open device in *fd_out it it succeeds.
 */
static struct wlr_device *open_if_kms(struct wlr_session *restrict session,
		const char *restrict path) {
	if (!path) {
		return NULL;
	}

	struct wlr_device *dev = wlr_session_open_file(session, path);
	if (!dev) {
		return NULL;
	}

	drmVersion *ver = drmGetVersion(dev->fd);
	if (!ver) {
		goto out_dev;
	}

	drmFreeVersion(ver);
	return dev;

out_dev:
	wlr_session_close_file(session, dev);
	return NULL;
}

static size_t explicit_find_gpus(struct wlr_session *session,
		size_t ret_len, struct wlr_device *ret[static ret_len], const char *str) {
	char *gpus = strdup(str);
	if (!gpus) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return 0;
	}

	size_t i = 0;
	char *save;
	char *ptr = strtok_r(gpus, ":", &save);
	do {
		if (i >= ret_len) {
			break;
		}

		ret[i] = open_if_kms(session, ptr);
		if (!ret[i]) {
			wlr_log(WLR_ERROR, "Unable to open %s as DRM device", ptr);
		} else {
			++i;
		}
	} while ((ptr = strtok_r(NULL, ":", &save)));

	free(gpus);
	return i;
}

/* Tries to find the primary GPU by checking for the "boot_vga" attribute.
 * If it's not found, it returns the first valid GPU it finds.
 */
size_t wlr_session_find_gpus(struct wlr_session *session,
		size_t ret_len, struct wlr_device **ret) {
	const char *explicit = getenv("WLR_DRM_DEVICES");
	if (explicit) {
		return explicit_find_gpus(session, ret_len, ret, explicit);
	}

	struct udev_enumerate *en = udev_enumerate_new(session->udev);
	if (!en) {
		wlr_log(WLR_ERROR, "Failed to create udev enumeration");
		return -1;
	}

	udev_enumerate_add_match_subsystem(en, "drm");
	udev_enumerate_add_match_sysname(en, "card[0-9]*");
	udev_enumerate_scan_devices(en);

	struct udev_list_entry *entry;
	size_t i = 0;

	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(en)) {
		if (i == ret_len) {
			break;
		}

		bool is_boot_vga = false;

		const char *path = udev_list_entry_get_name(entry);
		struct udev_device *dev = udev_device_new_from_syspath(session->udev, path);
		if (!dev) {
			continue;
		}

		const char *seat = udev_device_get_property_value(dev, "ID_SEAT");
		if (!seat) {
			seat = "seat0";
		}
		if (session->seat[0] && strcmp(session->seat, seat) != 0) {
			udev_device_unref(dev);
			continue;
		}

		// This is owned by 'dev', so we don't need to free it
		struct udev_device *pci =
			udev_device_get_parent_with_subsystem_devtype(dev, "pci", NULL);

		if (pci) {
			const char *id = udev_device_get_sysattr_value(pci, "boot_vga");
			if (id && strcmp(id, "1") == 0) {
				is_boot_vga = true;
			}
		}

		struct wlr_device *wlr_dev =
			open_if_kms(session, udev_device_get_devnode(dev));
		if (!wlr_dev) {
			udev_device_unref(dev);
			continue;
		}

		udev_device_unref(dev);

		ret[i] = wlr_dev;
		if (is_boot_vga) {
			struct wlr_device *tmp = ret[0];
			ret[0] = ret[i];
			ret[i] = tmp;
		}

		++i;
	}

	udev_enumerate_unref(en);

	return i;
}
