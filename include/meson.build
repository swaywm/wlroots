subdir('wlr')

exclude_files = ['meson.build', 'config.h.in', 'version.h.in']
if conf_data.get('WLR_HAS_X11_BACKEND', 0) != 1
	exclude_files += 'backend/x11.h'
endif
if conf_data.get('WLR_HAS_XWAYLAND', 0) != 1
	exclude_files += 'xwayland.h'
endif

install_subdir('wlr',
	install_dir: get_option('includedir'),
	exclude_files: exclude_files,
)
