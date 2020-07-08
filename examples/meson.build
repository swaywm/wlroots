threads = dependency('threads')
wayland_cursor = dependency('wayland-cursor')
libpng = dependency('libpng', required: false, disabler: true)
# These versions correspond to ffmpeg 4.0
libavutil = dependency('libavutil', version: '>=56.14.100', required: false, disabler: true)
libavcodec = dependency('libavcodec', version: '>=58.18.100', required: false, disabler: true)
libavformat = dependency('libavformat', version: '>=58.12.100', required: false, disabler: true)

# epoll is a separate library in FreeBSD
if host_machine.system() == 'freebsd'
	libepoll = dependency('epoll-shim')
else
	libepoll = dependency('', required: false)
endif

# Check if libavutil is found because of https://github.com/mesonbuild/meson/issues/6010
if libavutil.found() and not cc.has_header('libavutil/hwcontext_drm.h', dependencies: libavutil)
	libavutil = disabler()
endif

compositors = {
	'simple': {
		'src': 'simple.c',
	},
	'pointer': {
		'src': 'pointer.c',
	},
	'touch': {
		'src': ['touch.c', 'cat.c'],
	},
	'tablet': {
		'src': 'tablet.c',
	},
	'rotation': {
		'src': ['rotation.c', 'cat.c'],
	},
	'multi-pointer': {
		'src': 'multi-pointer.c',
	},
	'output-layout': {
		'src': ['output-layout.c', 'cat.c'],
	},
	'fullscreen-shell': {
		'src': 'fullscreen-shell.c',
		'proto': ['fullscreen-shell-unstable-v1'],
	},
}

clients = {
	'idle': {
		'src': 'idle.c',
		'dep': threads,
		'proto': ['kde-idle'],
	},
	'idle-inhibit': {
		'src': 'idle-inhibit.c',
		'dep': wlroots,
		'proto': [
			'idle-inhibit-unstable-v1',
			'xdg-shell',
		],
	},
	'keyboard-shortcuts-inhibit': {
		'src': 'keyboard-shortcuts-inhibit.c',
		'dep': [wayland_cursor, wlroots],
		'proto': [
			'keyboard-shortcuts-inhibit-unstable-v1',
			'xdg-shell',
		],
	},
	'layer-shell': {
		'src': 'layer-shell.c',
		'dep': [wayland_cursor, wlroots],
		'proto': [
			'wlr-layer-shell-unstable-v1',
			'xdg-shell',
		],
	},
	'input-inhibitor': {
		'src': 'input-inhibitor.c',
		'dep': [wayland_cursor, wlroots],
		'proto': [
			'wlr-input-inhibitor-unstable-v1',
			'xdg-shell',
		],
	},
	'gamma-control': {
		'src': 'gamma-control.c',
		'dep': [wayland_cursor, math],
		'proto': ['wlr-gamma-control-unstable-v1'],
	},
	'output-power-management': {
		'src': 'output-power-management.c',
		'dep': [wayland_client, wlroots],
		'proto': ['wlr-output-power-management-unstable-v1'],
	},
	'pointer-constraints': {
		'src': 'pointer-constraints.c',
		'dep': wlroots,
		'proto': [
			'pointer-constraints-unstable-v1',
			'xdg-shell',
		],
	},
	'relative-pointer': {
		'src': 'relative-pointer-unstable-v1.c',
		'dep': wlroots,
		'proto': [
			'pointer-constraints-unstable-v1',
			'relative-pointer-unstable-v1',
			'xdg-shell',
		],
	},
	'dmabuf-capture': {
		'src': 'dmabuf-capture.c',
		'dep': [
			libavcodec,
			libavformat,
			libavutil,
			drm.partial_dependency(compile_args: true), # <drm_fourcc.h>
			threads,
		],
		'proto': ['wlr-export-dmabuf-unstable-v1'],
	},
	'screencopy': {
		'src': 'screencopy.c',
		'dep': [libpng, rt],
		'proto': ['wlr-screencopy-unstable-v1'],
	},
	'screencopy-dmabuf': {
		'src': 'screencopy-dmabuf.c',
		'dep': [libpng, rt, gbm, drm],
		'proto': [
			'wlr-screencopy-unstable-v1',
			'linux-dmabuf-unstable-v1',
		],
	},
	'toplevel-decoration': {
		'src': 'toplevel-decoration.c',
		'dep': wlroots,
		'proto': [
			'xdg-decoration-unstable-v1',
			'xdg-shell',
		],
	},
	'input-method': {
		'src': 'input-method.c',
		'dep': libepoll,
		'proto': [
			'input-method-unstable-v2',
			'text-input-unstable-v3',
			'xdg-shell',
		],
	},
	'text-input': {
		'src': 'text-input.c',
		'dep': [wayland_cursor, wlroots],
		'proto': [
			'text-input-unstable-v3',
			'xdg-shell',
		],
	},
	'foreign-toplevel': {
		'src': 'foreign-toplevel.c',
		'dep': [wlroots],
		'proto': ['wlr-foreign-toplevel-management-unstable-v1'],
	},
	'virtual-pointer': {
		'src': 'virtual-pointer.c',
		'dep': wlroots,
		'proto': ['wlr-virtual-pointer-unstable-v1'],
	},
	'input-method-keyboard-grab': {
		'src': 'input-method-keyboard-grab.c',
		'dep': xkbcommon,
		'proto': [
			'input-method-unstable-v2',
		],
	},
}

foreach name, info : compositors
	extra_src = []
	foreach p : info.get('proto', [])
		extra_src += protocols_server_header[p]
	endforeach

	executable(
		name,
		[info.get('src'), extra_src],
		dependencies: wlroots,
		include_directories: [wlr_inc, proto_inc],
		build_by_default: get_option('examples'),
	)
endforeach

foreach name, info : clients
	extra_src = []
	foreach p : info.get('proto')
		extra_src += protocols_code[p]
		extra_src += protocols_client_header[p]
	endforeach

	executable(
		name,
		[info.get('src'), extra_src],
		dependencies: [wayland_client, info.get('dep')],
		build_by_default: get_option('examples'),
	)
endforeach
