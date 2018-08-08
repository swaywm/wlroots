glgen = find_program('../glgen.sh')

glapi = custom_target(
	'glapi',
	input: 'glapi.txt',
	output: ['@BASENAME@.c', '@BASENAME@.h'],
	command: [glgen, '@INPUT@', '@OUTDIR@'],
)

wlr_render_src = files(
	'dmabuf.c',
	'egl.c',
	'gles2/pixel_format.c',
	'gles2/renderer.c',
	'gles2/shaders.c',
	'gles2/texture.c',
	'gles2/util.c',
	'wlr_renderer.c',
	'wlr_texture.c'
)

wlr_render_deps = [egl, glesv2, pixman, wayland_server]
wlr_render_inc = [wlr_inc]

# vulkan
if conf_data.get('WLR_HAS_X11_BACKEND', false)
	subdir('vulkan/shaders')
	wlr_render_deps += [vulkan]
	wlr_render_src += files(
		'vulkan/renderer.c',
		'vulkan/vulkan.c',
		'vulkan/texture.c',
		'vulkan/pixel_format.c',
		'vulkan/util.c',
	)

	wlr_render_src += vulkan_shaders
	wlr_render_inc += include_directories('vulkan') # for shaders/ in builddir
endif

lib_wlr_render = static_library(
	'wlr_render',
	wlr_render_src,
	glapi,
	include_directories: wlr_render_inc,
	dependencies: wlr_render_deps,
)

wlr_render = declare_dependency(
	link_with: lib_wlr_render,
	sources: glapi[1],
)
