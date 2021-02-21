msg = []
if 'vulkan' in renderers
	msg += 'Install "@0@" or pass "-Dvulkan=disabled" to disable it.'
else
	msg += 'Required for vulkan renderer support.'
endif

dep_vulkan = dependency('vulkan',
	version: '>=1.2.182',
	required: 'vulkan' in renderers,
	not_found_message: '\n'.join(msg).format('vulkan')
)

if not dep_vulkan.found()
	subdir_done()
endif

glslang = find_program('glslangValidator', native: true, required: false)
if not glslang.found()
	if 'vulkan' in renderers
		error('\n'.join(msg).format('glslang'))
	else
		subdir_done()
	endif
endif

wlr_files += files(
	'renderer.c',
	'texture.c',
	'vulkan.c',
	'util.c',
	'pixel_format.c',
)

wlr_deps += dep_vulkan
features += { 'vulkan-renderer': true }

subdir('shaders')
