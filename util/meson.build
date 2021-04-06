wlr_files += files(
	'array.c',
	'global.c',
	'log.c',
	'region.c',
	'shm.c',
	'signal.c',
	'time.c',
)


if features.get('xdg-foreign')
	add_project_arguments('-DHAS_LIBUUID=@0@'.format(uuid.found().to_int()), language: 'c')
	wlr_deps += uuid
	wlr_files += files('uuid.c')
endif
