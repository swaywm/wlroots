lib_wlr_xcursor = static_library(
	'wlr_xcursor',
	files(
		'wlr_xcursor.c',
		'xcursor.c',
	),
	include_directories: wlr_inc,
	dependencies: [egl] # header required via include/wlr/render.h
)
