backend_files = files(
	'backend.c',
	'drm/atomic.c',
	'drm/backend.c',
	'drm/drm.c',
	'drm/legacy.c',
	'drm/properties.c',
	'drm/renderer.c',
	'drm/util.c',
	'headless/backend.c',
	'headless/input_device.c',
	'headless/output.c',
	'libinput/backend.c',
	'libinput/events.c',
	'libinput/keyboard.c',
	'libinput/pointer.c',
	'libinput/tablet_pad.c',
	'libinput/tablet_tool.c',
	'libinput/touch.c',
	'multi/backend.c',
	'session/direct-ipc.c',
	'session/session.c',
	'wayland/backend.c',
	'wayland/output.c',
	'wayland/registry.c',
	'wayland/wl_seat.c',
)

backend_deps = [
	drm,
	egl,
	gbm,
	libinput,
	pixman,
	xkbcommon,
	wayland_server,
	wlr_protos,
	wlr_render,
]

if host_machine.system().startswith('freebsd')
	backend_files += files('session/direct-freebsd.c')
else
	backend_files += files('session/direct.c')
endif

if conf_data.get('WLR_HAS_SYSTEMD', false)
	backend_files += files('session/logind.c')
	backend_deps += systemd
endif

if conf_data.get('WLR_HAS_X11_BACKEND', false)
	backend_files += files(
		'x11/backend.c',
		'x11/input_device.c',
		'x11/output.c',
	)
	backend_deps += xcb_xkb
endif

if conf_data.get('WLR_HAS_ELOGIND', false)
	backend_files += files('session/logind.c')
	backend_deps += elogind
endif

lib_wlr_backend = static_library(
	'wlr_backend',
	backend_files,
	include_directories: wlr_inc,
	dependencies: backend_deps,
)
