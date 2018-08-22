threads = dependency('threads')
wayland_cursor = dependency('wayland-cursor')
libpng = dependency('libpng', required: false)
# These versions correspond to ffmpeg 4.0
libavutil = dependency('libavutil', version: '>=56.14.100', required: false)
libavcodec = dependency('libavcodec', version: '>=58.18.100', required: false)
libavformat = dependency('libavformat', version: '>=58.12.100', required: false)

# Small hack until https://github.com/mesonbuild/meson/pull/3386/ is merged
foreach dep : ['libpng', 'libavutil', 'libavcodec', 'libavformat']
	if not get_variable(dep).found()
		set_variable(dep, disabler())
	endif
endforeach

if not cc.has_header('libavutil/hwcontext_drm.h', dependencies: libavutil)
	libavutil = disabler()
endif

examples = {
	'simple': {
		'src': 'simple.c',
		'dep': wlroots,
	},
	'pointer': {
		'src': 'pointer.c',
		'dep': wlroots,
	},
	'touch': {
		'src': ['touch.c', 'cat.c'],
		'dep': wlroots,
	},
	'tablet': {
		'src': 'tablet.c',
		'dep': wlroots,
	},
	'rotation': {
		'src': ['rotation.c', 'cat.c'],
		'dep': wlroots,
	},
	'multi-pointer': {
		'src': 'multi-pointer.c',
		'dep': wlroots,
	},
	'output-layout': {
		'src': ['output-layout.c', 'cat.c'],
		'dep': wlroots,
	},
	'screenshot': {
		'src': 'screenshot.c',
		'dep': [wayland_client, wlr_protos, wlroots],
	},
	'idle': {
		'src': 'idle.c',
		'dep': [wayland_client, wlr_protos, wlroots, threads],
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
		'dep': [wayland_client, wayland_cursor, wlr_protos, wlroots],
	},
	'dmabuf-capture': {
		'src': 'dmabuf-capture.c',
		'dep': [
			libavcodec,
			libavformat,
			libavutil,
			threads,
			wayland_client,
			wlr_protos,
			wlroots,
		],
	},
	'screencopy': {
		'src': 'screencopy.c',
		'dep': [libpng, wayland_client, wlr_protos, wlroots],
	},
	'toplevel-decoration': {
		'src': 'toplevel-decoration.c',
		'dep': [wayland_client, wlr_protos, wlroots],
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
