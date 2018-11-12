xwayland_libs = []
xwayland_required = [
	'xcb',
	'xcb-composite',
	'xcb-render',
	'xcb-xfixes',
]
xwayland_optional = [
	'xcb-errors',
	'xcb-icccm',
]

foreach lib : xwayland_required
	dep = dependency(lib, required: get_option('xwayland'))
	if not dep.found()
		subdir_done()
	endif

	xwayland_libs += dep
endforeach

foreach lib : xwayland_optional
	dep = dependency(lib, required: get_option(lib))
	if dep.found()
		xwayland_libs += dep
		conf_data.set10('WLR_HAS_' + lib.underscorify().to_upper(), true)
	endif
endforeach

lib_wlr_xwayland = static_library(
	'wlr_xwayland',
	files(
		'selection/dnd.c',
		'selection/incoming.c',
		'selection/outgoing.c',
		'selection/selection.c',
		'sockets.c',
		'xwayland.c',
		'xwm.c',
	),
	include_directories: wlr_inc,
	dependencies: [
		wayland_server,
		xwayland_libs,
		xkbcommon,
		pixman,
	],
)

wlr_parts += lib_wlr_xwayland
conf_data.set10('WLR_HAS_XWAYLAND', true)
