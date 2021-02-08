msg = []
if get_option('gles2-renderer').enabled()
	msg += 'Install "glesv2" or pass "-Dgles2-renderer=disabled".'
endif
if not get_option('gles2-renderer').disabled()
	msg += 'Required for GLES2 support.'
endif

glesv2 = dependency('glesv2',
	required: get_option('gles2-renderer'),
	not_found_message: '\n'.join(msg),
)
if not glesv2.found()
	subdir_done()
endif

features += { 'gles2-renderer': true }

wlr_deps += glesv2

wlr_files += files(
	'pixel_format.c',
	'renderer.c',
	'shaders.c',
	'texture.c',
)
