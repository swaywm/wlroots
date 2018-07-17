lib_wlr_util = static_library(
	'wlr_util',
	files(
		'array.c',
		'log.c',
		'os-compatibility.c',
		'region.c',
		'signal.c',
	),
	include_directories: wlr_inc,
	dependencies: [wayland_server, pixman],
)
