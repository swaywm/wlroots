if xwayland.found()
	xwayland_path = xwayland.get_pkgconfig_variable('xwayland')
else
	xwayland_path = xwayland_prog.full_path()
endif

xwayland_config_data = configuration_data()
xwayland_config_data.set_quoted('XWAYLAND_PATH', xwayland_path)
configure_file(
	output: 'config.h',
	configuration: xwayland_config_data,
)
