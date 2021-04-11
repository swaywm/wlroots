libseat = dependency('libseat',
	version: '>=0.2.0',
	fallback: ['seatd', 'libseat'],
	default_options: ['server=disabled', 'man-pages=disabled'],
)

wlr_files += files(
	'direct-ipc.c',
	'noop.c',
	'session.c',
	'libseat.c'
)

if host_machine.system().startswith('freebsd')
	wlr_files += files('direct-freebsd.c')
else
	wlr_files += files('direct.c')
endif

wlr_deps += libseat
