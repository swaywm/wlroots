backend_parts = []
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
	'libinput/switch.c',
	'libinput/tablet_pad.c',
	'libinput/tablet_tool.c',
	'libinput/touch.c',
	'multi/backend.c',
	'session/direct-ipc.c',
	'session/session.c',
	'wayland/backend.c',
	'wayland/output.c',
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

if logind.found()
	backend_files += files('session/logind.c')
	backend_deps += logind
endif

subdir('x11')

lib_wlr_backend = static_library(
	'wlr_backend',
	backend_files,
	include_directories: wlr_inc,
	link_whole: backend_parts,
	dependencies: backend_deps,
)
