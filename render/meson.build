renderers = get_option('renderers')
if 'auto' in renderers
	renderers = ['gles2']
endif

gbm = dependency('gbm', required : false)
egl = disabler()
glesv2 = disabler()

wlr_files += files(
	'allocator.c',
	'dmabuf.c',
	'drm_format_set.c',
	'pixel_format.c',
	'shm_allocator.c',
	'swapchain.c',
	'wlr_renderer.c',
	'wlr_texture.c',
	'drm_dumb_allocator.c',
)
wlr_deps += drm_headers

if gbm.found()
  wlr_files += files(
	  'gbm_allocator.c',
  )
  features += { 'gbm-support': true }
  wlr_deps += [gbm]

  code = '''
#include <gbm.h>
int main() {
  struct gbm_device *gbm = NULL;
  struct gbm_bo *bo = NULL;
  struct gbm_import_fd_modifier_data data = {0};
  gbm_bo_import(gbm, GBM_BO_IMPORT_FD_MODIFIER, &data, GBM_BO_USE_SCANOUT);
  union gbm_bo_handle plane_handle = gbm_bo_get_handle_for_plane(bo, i);
}
'''
# For those proprietary drivers' sake
  result = cc.links(code, dependencies: [gbm], name: 'Check whether the GBM is up to date')
  if result
    features += { 'latest-gbm': true }
  endif

# Actually, a GLES renderer could still render into a memory buffer
# while those headless device would work in this way
  if ['gles2', 'auto'].contains(renderers)
    subdir('gles2')
  endif
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
