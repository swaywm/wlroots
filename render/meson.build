renderers = get_option('renderers')

if 'auto' in renderers and get_option('auto_features').disabled()
  warning('The global auto features is set to disabled but we would still enable those renderers if their dependencies are met')
endif

if 'auto' in renderers
	renderers = ['gles2']
endif

wlr_files += files(
	'allocator.c',
	'dmabuf.c',
	'drm_format_set.c',
	'gbm_allocator.c',
	'pixel_format.c',
	'shm_allocator.c',
	'swapchain.c',
	'wlr_renderer.c',
	'wlr_texture.c',
	'drm_dumb_allocator.c',
)

egl = dependency('egl', required: 'gles2' in renderers)
if egl.found()
	wlr_deps += egl
	wlr_files += files('egl.c')
endif

if 'gles2' in renderers or 'auto' in renderers
	subdir('gles2')
endif

subdir('pixman')
