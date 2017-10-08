glgen = find_program('../glgen.sh')

glapi_c = custom_target('glapi.c',
	input: 'glapi.txt',
	output: '@BASENAME@.c',
	command: [glgen, '@INPUT@', '@OUTPUT@'],
)
glapi_h = custom_target('glapi.h',
	input: 'glapi.txt',
	output: '@BASENAME@.h',
	command: [glgen, '@INPUT@', '@OUTPUT@'],
)

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
	glapi_c,
	glapi_h,
	include_directories: wlr_inc,
	dependencies: [glesv2, egl],
)

wlr_render = declare_dependency(
	link_with: lib_wlr_render,
	sources: glapi_h,
)
