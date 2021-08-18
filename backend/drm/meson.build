libliftoff = dependency('libliftoff', version: '>=0.2.0', fallback: ['libliftoff', 'liftoff'])

wlr_files += files(
	'atomic.c',
	'backend.c',
	'cvt.c',
	'drm.c',
	'legacy.c',
	'libliftoff.c',
	'properties.c',
	'renderer.c',
	'util.c',
)

features += { 'drm-backend': true }
wlr_deps += libliftoff
