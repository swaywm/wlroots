threads = dependency('threads')
wayland_cursor = dependency('wayland-cursor')
libpng = dependency('libpng', required: false, disabler: true)
# These versions correspond to ffmpeg 4.0
libavutil = dependency('libavutil', version: '>=56.14.100', required: false, disabler: true)
libavcodec = dependency('libavcodec', version: '>=58.18.100', required: false, disabler: true)
libavformat = dependency('libavformat', version: '>=58.12.100', required: false, disabler: true)

# epoll is a separate library in FreeBSD
if host_machine.system() == 'freebsd'
	libepoll = [dependency('epoll-shim')]
else
	libepoll = []
endif

# Check if libavutil is found because of https://github.com/mesonbuild/meson/issues/6010
if libavutil.found() and not cc.has_header('libavutil/hwcontext_drm.h', dependencies: libavutil)
	libavutil = disabler()
endif

examples = {
	'simple': {
		'src': 'simple.c',
		'dep': [wlroots],
	},
	'pointer': {
		'src': 'pointer.c',
		'dep': [wlroots],
	},
	'touch': {
		'src': ['touch.c', 'cat.c'],
		'dep': [wlroots],
	},
	'tablet': {
		'src': 'tablet.c',
		'dep': [wlroots],
	},
	'rotation': {
		'src': ['rotation.c', 'cat.c'],
		'dep': [wlroots],
	},
	'multi-pointer': {
		'src': 'multi-pointer.c',
		'dep': [wlroots],
	},
	'output-layout': {
		'src': ['output-layout.c', 'cat.c'],
		'dep': [wlroots],
	},
	'idle': {
		'src': 'idle.c',
		'dep': [wayland_client, wlr_protos, threads],
	},
	'idle-inhibit': {
		'src': 'idle-inhibit.c',
		'dep': [wayland_client, wlr_protos, wlroots],
	},
	'layer-shell': {
		'src': 'layer-shell.c',
		'dep': [wayland_client, wayland_cursor, wlr_protos, wlroots],
	},
	'input-inhibitor': {
		'src': 'input-inhibitor.c',
		'dep': [wayland_client, wayland_cursor, wlr_protos, wlroots],
	},
	'gamma-control': {
		'src': 'gamma-control.c',
		'dep': [wayland_client, wayland_cursor, wlr_protos, math],
	},
	'pointer-constraints': {
		'src': 'pointer-constraints.c',
		'dep': [wayland_client, wlr_protos, wlroots],
	},
	'relative-pointer': {
		'src': 'relative-pointer-unstable-v1.c',
		'dep': [wayland_client, wlr_protos, wlroots],
	},
	'dmabuf-capture': {
		'src': 'dmabuf-capture.c',
		'dep': [
			libavcodec,
			libavformat,
			libavutil,
			drm.partial_dependency(compile_args: true), # <drm_fourcc.h>
			threads,
			wayland_client,
			wlr_protos,
		],
	},
	'screencopy': {
		'src': 'screencopy.c',
		'dep': [libpng, wayland_client, wlr_protos, rt],
	},
	'toplevel-decoration': {
		'src': 'toplevel-decoration.c',
		'dep': [wayland_client, wlr_protos, wlroots],
	},
	'input-method': {
		'src': 'input-method.c',
		'dep': [wayland_client, wlr_protos] + libepoll,
	},
	'text-input': {
		'src': 'text-input.c',
		'dep': [wayland_cursor, wayland_client, wlr_protos, wlroots],
	},
	'foreign-toplevel': {
		'src': 'foreign-toplevel.c',
		'dep': [wayland_client, wlr_protos, wlroots],
	},
	'fullscreen-shell': {
		'src': 'fullscreen-shell.c',
		'dep': [wlr_protos, wlroots],
	},
}

foreach name, info : examples
	executable(
		name,
		info.get('src'),
		dependencies: info.get('dep'),
		build_by_default: get_option('examples'),
	)
endforeach
