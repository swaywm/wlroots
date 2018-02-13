glgen = find_program('../glgen.sh')

glapi = custom_target('glapi',
	input: 'glapi.txt',
	output: ['@BASENAME@.c', '@BASENAME@.h'],
	command: [glgen, '@INPUT@', '@OUTPUT0@', '@OUTPUT1@'],
)

lib_wlr_render = static_library(
	'wlr_render',
	files(
		'egl.c',
		'gles2/pixel_format.c',
		'gles2/renderer.c',
		'gles2/shaders.c',
		'gles2/texture.c',
		'gles2/util.c',
		'matrix.c',
		'wlr_renderer.c',
		'wlr_texture.c',
	),
	glapi[0],
	glapi[1],
	include_directories: wlr_inc,
	dependencies: [glesv2, egl, pixman],
)

wlr_render = declare_dependency(
	link_with: lib_wlr_render,
	sources: glapi[1],
)
