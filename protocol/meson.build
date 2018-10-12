wl_protocol_dir = wayland_protos.get_pkgconfig_variable('pkgdatadir')

wayland_scanner_dep = dependency('wayland-scanner', required: false, native: true)
if wayland_scanner_dep.found()
	wayland_scanner = find_program(
		wayland_scanner_dep.get_pkgconfig_variable('wayland_scanner'),
		native: true,
	)
else
	wayland_scanner = find_program('wayland-scanner', native: true)
endif

protocols = [
	[wl_protocol_dir, 'stable/presentation-time/presentation-time.xml'],
	[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
	[wl_protocol_dir, 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/tablet/tablet-unstable-v2.xml'],
	[wl_protocol_dir, 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/xdg-output/xdg-output-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/xdg-shell/xdg-shell-unstable-v6.xml'],
	[wl_protocol_dir, 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
	'gamma-control.xml',
	'gtk-primary-selection.xml',
	'idle.xml',
	'input-method-unstable-v2.xml',
	'screenshooter.xml',
	'server-decoration.xml',
	'text-input-unstable-v3.xml',
	'virtual-keyboard-unstable-v1.xml',
	'wlr-export-dmabuf-unstable-v1.xml',
	'wlr-gamma-control-unstable-v1.xml',
	'wlr-input-inhibitor-unstable-v1.xml',
	'wlr-layer-shell-unstable-v1.xml',
	'wlr-screencopy-unstable-v1.xml',
]

client_protocols = [
	[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
	[wl_protocol_dir, 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/xdg-shell/xdg-shell-unstable-v6.xml'],
	[wl_protocol_dir, 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
	'idle.xml',
	'input-method-unstable-v2.xml',
	'screenshooter.xml',
	'text-input-unstable-v3.xml',
	'wlr-export-dmabuf-unstable-v1.xml',
	'wlr-gamma-control-unstable-v1.xml',
	'wlr-input-inhibitor-unstable-v1.xml',
	'wlr-layer-shell-unstable-v1.xml',
	'wlr-screencopy-unstable-v1.xml',
]

wl_protos_src = []
wl_protos_headers = []

foreach p : protocols
	xml = join_paths(p)
	wl_protos_src += custom_target(
		xml.underscorify() + '_server_c',
		input: xml,
		output: '@BASENAME@-protocol.c',
		command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
	)
	wl_protos_headers += custom_target(
		xml.underscorify() + '_server_h',
		input: xml,
		output: '@BASENAME@-protocol.h',
		command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
	)
endforeach

foreach p : client_protocols
	xml = join_paths(p)
	wl_protos_headers += custom_target(
		xml.underscorify() + '_client_h',
		input: xml,
		output: '@BASENAME@-client-protocol.h',
		command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
	)
endforeach

lib_wl_protos = static_library(
	'wl_protos',
	wl_protos_src + wl_protos_headers,
	dependencies: wayland_client.partial_dependency(compile_args: true),
)

wlr_protos = declare_dependency(
	link_with: lib_wl_protos,
	sources: wl_protos_headers,
)
