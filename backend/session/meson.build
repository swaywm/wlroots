libseat = dependency('libseat',
	version: '>=0.2.0',
	fallback: ['seatd', 'libseat'],
	default_options: ['server=disabled', 'man-pages=disabled'],
	required: get_option('session'),
)
if libseat.found()
	wlr_files += files('session.c')
	wlr_deps += libseat
	features += { 'session': true }
else
	features += { 'session': false }
endif
