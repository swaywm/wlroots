renderers = get_option('renderers')
if 'auto' in renderers and get_option('auto_features').enabled()
	renderers = ['gles2']
elif 'auto' in renderers and get_option('auto_features').disabled()
	renderers = []
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

has = cc.has_function('gbm_bo_get_fd_for_plane', dependencies: [gbm])
add_project_arguments('-DHAS_GBM_BO_GET_FD_FOR_PLANE=@0@'.format(has.to_int()), language: 'c')

egl = dependency('egl', required: 'gles2' in renderers)
if egl.found()
	wlr_deps += egl
	wlr_files += files('egl.c')
endif

if 'gles2' in renderers or 'auto' in renderers
	subdir('gles2')
endif

subdir('pixman')
