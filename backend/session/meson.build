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

# logind

msg = []
if get_option('logind').enabled()
	msg += 'Install "lib@0@" or pass "-Dlogind=disabled".'
endif
if not get_option('logind').disabled()
	msg += 'Required for logind support.'
	msg += 'You may need to pass "-Dlogind-provider=elogind" or "-Dlogind-provider=systemd" to ensure the correct library is detected.'
endif

logind = dependency('lib' + get_option('logind-provider'),
	required: get_option('logind'),
	not_found_message: '\n'.join(msg).format(get_option('logind-provider')),
	version: '>=237',
)
if logind.found()
	wlr_files += files('logind.c')
	wlr_deps += logind
	conf_data.set10('WLR_HAS_' + get_option('logind-provider').to_upper(), true)
endif

# libcap

msg = []
if get_option('libcap').enabled()
	msg += 'Install "libcap" or pass "-Dlibcap=disabled".'
endif
if not get_option('libcap').disabled()
	msg += 'Required for POSIX capability support (Not needed if using logind).'
endif

libcap = dependency('libcap',
	required: get_option('libcap'),
	not_found_message: '\n'.join(msg),
)
if libcap.found()
	conf_data.set10('WLR_HAS_LIBCAP', true)
	wlr_deps += libcap
endif

