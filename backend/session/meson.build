libseat = dependency('libseat',
	version: '>=0.2.0',
	fallback: ['seatd', 'libseat'],
	default_options: ['server=disabled', 'man-pages=disabled'],
)

wlr_files += files(
	'noop.c',
	'session.c',
	'libseat.c'
)

wlr_deps += libseat
