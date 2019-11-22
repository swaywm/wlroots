rdp_libs = []
rdp_required = [
	'freerdp2',
	'winpr2',
]

msg = []
if get_option('freerdp').enabled()
	msg += 'Install "@0@" or pass "-Dfreerdp=disabled".'
endif
if not get_option('freerdp').disabled()
	msg += 'Required for RDP backend support.'
endif

foreach lib : rdp_required
	dep = dependency(lib,
		required: get_option('freerdp'),
		not_found_message: '\n'.join(msg).format(lib),
	)
	if not dep.found()
		subdir_done()
	endif

	rdp_libs += dep
endforeach

wlr_files += files(
	'backend.c',
	'keyboard.c',
	'listener.c',
	'output.c',
	'peer.c',
	'pointer.c',
)
wlr_deps += rdp_libs
conf_data.set10('WLR_HAS_RDP_BACKEND', true)
