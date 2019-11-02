x11_libs = []
x11_required = [
	'x11-xcb',
	'xcb',
	'xcb-present',
	'xcb-xfixes',
	'xcb-xinput',
]

foreach lib : x11_required
	dep = dependency(lib, required: get_option('x11-backend'))
	if not dep.found()
		subdir_done()
	endif

	x11_libs += dep
endforeach

lib_wlr_backend_x11 = static_library(
	'wlr_backend_x11',
	files(
		'backend.c',
		'input_device.c',
		'output.c',
	),
	include_directories: wlr_inc,
	dependencies: [
		wayland_server,
		pixman,
		xkbcommon,
		x11_libs,
	],
)

backend_parts += lib_wlr_backend_x11
conf_data.set10('WLR_HAS_X11_BACKEND', true)
