backend_files = files(
	'backend.c',
	'session/direct-ipc.c',
	'session/session.c',
	'drm/atomic.c',
	'drm/backend.c',
	'drm/drm.c',
	'drm/legacy.c',
	'drm/properties.c',
	'drm/renderer.c',
	'drm/util.c',
	'headless/backend.c',
	'headless/output.c',
	'headless/input_device.c',
	'libinput/backend.c',
	'libinput/events.c',
	'libinput/keyboard.c',
	'libinput/pointer.c',
	'libinput/tablet_pad.c',
	'libinput/tablet_tool.c',
	'libinput/touch.c',
	'multi/backend.c',
	'wayland/backend.c',
	'wayland/output.c',
	'wayland/registry.c',
	'wayland/wl_seat.c',
	'x11/backend.c',
)

if host_machine.system().startswith('freebsd')
	backend_files += files('session/direct-freebsd.c')
else
	backend_files += files('session/direct.c')
endif

if systemd.found()
	backend_files += files('session/logind.c')
endif

if elogind.found()
	backend_files += files('session/logind.c')
endif

lib_wlr_backend = static_library(
	'wlr_backend',
	backend_files,
	include_directories: wlr_inc,
	dependencies: [wayland_server, egl, gbm, libinput, systemd, elogind, wlr_render, wlr_protos],
)
