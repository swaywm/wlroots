glgen = find_program('../glgen.sh')

glapi = custom_target(
	'glapi',
	input: 'glapi.txt',
	output: ['@BASENAME@.c', '@BASENAME@.h'],
	command: [glgen, '@INPUT@', '@OUTDIR@'],
)

lib_wlr_render = static_library(
	'wlr_render',
	files(
		'dmabuf.c',
		'egl.c',
		'gles2/pixel_format.c',
		'gles2/renderer.c',
		'gles2/shaders.c',
		'gles2/texture.c',
		'gles2/util.c',
		'wlr_renderer.c',
		'wlr_texture.c',
	),
	glapi,
	include_directories: wlr_inc,
	dependencies: [egl, glesv2, pixman, wayland_server],
)

wlr_render = declare_dependency(
	link_with: lib_wlr_render,
	sources: glapi[1],
)
