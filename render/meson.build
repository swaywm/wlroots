renderers = get_option('renderers')
if 'auto' in renderers
	renderers = ['gles2']
endif

gbm = dependency('gbm', version: '>=17.1.0', required : false)

wlr_files += files(
	'allocator.c',
	'dmabuf.c',
	'drm_format_set.c',
	'pixel_format.c',
	'shm_allocator.c',
	'swapchain.c',
	'wlr_renderer.c',
	'wlr_texture.c',
)
wlr_deps += drm_headers

if gbm.found()
  wlr_files += files(
	  'gbm_allocator.c',
  )
  features += { 'gbm-support': true }
  wlr_deps += [gbm]
endif

egl = dependency('egl', required: 'gles2' in renderers)
if egl.found()
	wlr_deps += egl
	wlr_files += files('egl.c')
endif

if 'gles2' in renderers or 'auto' in renderers
	subdir('gles2')
endif

subdir('pixman')
