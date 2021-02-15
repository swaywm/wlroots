subdir('wlr')

exclude_files = ['meson.build', 'config.h.in', 'version.h.in']
if not features.get('x11-backend')
	exclude_files += 'backend/x11.h'
endif
if not features.get('xwayland')
	exclude_files += 'xwayland.h'
endif
if not features.get('xdg-foreign')
	exclude_files += [
		'types/wlr_xdg_foreign_v1.h',
		'types/wlr_xdg_foreign_v2.h',
		'types/wlr_xdg_foreign_registry.h',
	]
endif

install_subdir('wlr',
	install_dir: get_option('includedir'),
	exclude_files: exclude_files,
)
