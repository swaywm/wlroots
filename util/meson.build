wlr_files += files(
	'array.c',
	'log.c',
	'region.c',
	'shm.c',
	'signal.c',
)

if uuid.found()
	wlr_deps += uuid
	add_project_arguments('-DHAS_LIBUUID', language: 'c')
endif

if conf_data.get('WLR_HAS_XDG_FOREIGN', 0) == 1
	wlr_files += files(
			'uuid.c',
		)
endif
