wlr_files += files(
	'array.c',
	'global.c',
	'log.c',
	'region.c',
	'shm.c',
	'signal.c',
	'time.c',
	'trace.c',
)

if features.get('xdg-foreign')
	if uuid.found()
		wlr_deps += uuid
		add_project_arguments('-DHAS_LIBUUID=1', language: 'c')
	else
		add_project_arguments('-DHAS_LIBUUID=0', language: 'c')
	endif
	wlr_files += files('uuid.c')
endif
