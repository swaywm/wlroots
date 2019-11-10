lib_wlr_render = static_library(
	'wlr_render',
	files(
		'dmabuf.c',
		'egl.c',
		'drm_format_set.c',
		'gles2/pixel_format.c',
		'gles2/renderer.c',
		'gles2/shaders.c',
		'gles2/texture.c',
		'wlr_renderer.c',
		'wlr_texture.c',
	),
	include_directories: wlr_inc,
	dependencies: [
		egl,
		drm.partial_dependency(compile_args: true), # <drm_fourcc.h>
		glesv2,
		pixman,
		wayland_server,
	],
)

wlr_render = declare_dependency(
	link_with: lib_wlr_render,
)
