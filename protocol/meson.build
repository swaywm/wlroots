wl_protocol_dir = wayland_protos.get_pkgconfig_variable('pkgdatadir')

wl_scanner = find_program('wayland-scanner', required: false)
if not wl_scanner.found()
	wl_scanner_dep = dependency('wayland-scanner')
	wl_scanner = find_program(wl_scanner_dep.get_pkgconfig_variable('wayland_scanner'))
endif

wl_scanner_version = run_command(wl_scanner, '-v').stderr().strip().split(' ')[-1]
code_type = wl_scanner_version.version_compare('>=1.14.91') ? 'public-code' : 'code'

protocols = [
	[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
	[wl_protocol_dir, 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/xdg-shell/xdg-shell-unstable-v6.xml'],
	[wl_protocol_dir, 'unstable/xdg-output/xdg-output-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/tablet/tablet-unstable-v2.xml'],
	'gamma-control.xml',
	'gtk-primary-selection.xml',
	'idle.xml',
	'screenshooter.xml',
	'server-decoration.xml',
	'virtual-keyboard-unstable-v1.xml',
	'wlr-export-dmabuf-unstable-v1.xml',
	'wlr-input-inhibitor-unstable-v1.xml',
	'wlr-layer-shell-unstable-v1.xml',
	'wlr-screencopy-unstable-v1.xml',
]

client_protocols = [
	[wl_protocol_dir, 'unstable/xdg-shell/xdg-shell-unstable-v6.xml'],
	[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
	[wl_protocol_dir, 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'],
	'idle.xml',
	'screenshooter.xml',
	'wlr-export-dmabuf-unstable-v1.xml',
	'wlr-input-inhibitor-unstable-v1.xml',
	'wlr-layer-shell-unstable-v1.xml',
	'wlr-screencopy-unstable-v1.xml',
]

wl_protos_src = []
wl_protos_headers = []

foreach p : protocols
	xml = join_paths(p)
	name = xml.split('/')[-1].split('.')[0]

	wl_protos_src += custom_target(name + '-protocol.c',
		input: xml,
		output: '@BASENAME@-protocol.c',
		command: [wl_scanner, code_type, '@INPUT@', '@OUTPUT@'],
	)
	wl_protos_headers += custom_target(name + '-protocol.h',
		input: xml,
		output: '@BASENAME@-protocol.h',
		command: [wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
		install: true,
		install_dir: join_paths(get_option('includedir'), 'wlr/protocol'),
	)
endforeach

foreach p : client_protocols
	xml = join_paths(p)
	name = xml.split('/')[-1].split('.')[0]

	wl_protos_headers += custom_target(name + '-client-protocol.h',
		input: xml,
		output: '@BASENAME@-client-protocol.h',
		command: [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
	)
endforeach

lib_wl_protos = static_library('wl_protos', wl_protos_src + wl_protos_headers,
	dependencies: [wayland_client]) # for the include directory

wlr_protos = declare_dependency(
	link_with: lib_wl_protos,
	sources: wl_protos_headers,
)

# Symbolic link so these appear as <wlr/protocol/foobar.h> for build
meson.add_postconf_script('ln', '-s',
	'../../protocol',
	join_paths(meson.build_root(), 'include/wlr/protocol'),
)
