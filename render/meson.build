lib_wlr_render = static_library(
	'wlr_render',
	files(
		'egl.c',
		'matrix.c',
		'gles2/pixel_format.c',
		'gles2/renderer.c',
		'gles2/shaders.c',
		'gles2/texture.c',
		'gles2/util.c',
		'wlr_renderer.c',
		'wlr_texture.c',
	),
	include_directories: wlr_inc,
	dependencies: [glesv2, egl],
)
