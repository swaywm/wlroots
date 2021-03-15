wlr_files += files(
	'direct-ipc.c',
	'noop.c',
	'session.c',
)

if host_machine.system().startswith('freebsd')
	wlr_files += files('direct-freebsd.c')
else
	wlr_files += files('direct.c')
endif

# libseat

libseat = dependency('libseat',
	required: get_option('libseat'),
	version: '>=0.2.0',
	fallback: ['seatd', 'libseat'],
	default_options: ['server=disabled', 'man-pages=disabled'],
)
if libseat.found()
	wlr_files += files('libseat.c')
	wlr_deps += libseat
	features += { 'libseat': true }
endif
