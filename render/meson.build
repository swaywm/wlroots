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
		'render.c',
		'shaders.c',
		'tex.c',
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
