project(
	'wlroots',
	'c',
	version: '0.4.1',
	license: 'MIT',
	meson_version: '>=0.48.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

# Format of so_version is CURRENT, REVISION, AGE.
# See: https://autotools.io/libtool/version.html
# for a reference about clean library versioning.
so_version = ['1', '2', '0']

add_project_arguments([
	'-DWLR_SRC_DIR="@0@"'.format(meson.current_source_dir()),
	'-DWLR_USE_UNSTABLE',
], language: 'c')

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
	'-Wundef',
	'-Wlogical-op',
	'-Wmissing-include-dirs',
	'-Wold-style-definition',
	'-Wpointer-arith',
	'-Winit-self',
	'-Wstrict-prototypes',
	'-Wredundant-decls',
	'-Wimplicit-fallthrough=2',
	'-Wendif-labels',
	'-Wstrict-aliasing=2',
	'-Woverflow',

	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
]), language: 'c')

conf_data = configuration_data()
conf_data.set10('WLR_HAS_LIBCAP', false)
conf_data.set10('WLR_HAS_SYSTEMD', false)
conf_data.set10('WLR_HAS_ELOGIND', false)
conf_data.set10('WLR_HAS_X11_BACKEND', false)
conf_data.set10('WLR_HAS_XWAYLAND', false)
conf_data.set10('WLR_HAS_XCB_ERRORS', false)
conf_data.set10('WLR_HAS_XCB_ICCCM', false)

wlr_inc = include_directories('.', 'include')

# Clang complains about some zeroed initializer lists (= {0}), even though they
# are valid
if cc.get_id() == 'clang'
	add_project_arguments('-Wno-missing-field-initializers', language: 'c')
	add_project_arguments('-Wno-missing-braces', language: 'c')
endif

# Avoid wl_buffer deprecation warnings
add_project_arguments('-DWL_HIDE_DEPRECATED', language: 'c')

wayland_server = dependency('wayland-server', version: '>=1.16')
wayland_client = dependency('wayland-client')
wayland_egl    = dependency('wayland-egl')
wayland_protos = dependency('wayland-protocols', version: '>=1.17')
egl            = dependency('egl')
glesv2         = dependency('glesv2')
drm            = dependency('libdrm', version: '>=2.4.95')
gbm            = dependency('gbm', version: '>=17.1.0')
libinput       = dependency('libinput', version: '>=1.7.0')
xkbcommon      = dependency('xkbcommon')
udev           = dependency('libudev')
pixman         = dependency('pixman-1')
libcap         = dependency('libcap', required: get_option('libcap'))
logind         = dependency('lib' + get_option('logind-provider'), required: get_option('logind'), version: '>=237')
math           = cc.find_library('m')
rt             = cc.find_library('rt')

wlr_parts = []
wlr_deps = []

if libcap.found()
	conf_data.set10('WLR_HAS_LIBCAP', true)
	wlr_deps += libcap
endif

if logind.found()
	conf_data.set10('WLR_HAS_' + get_option('logind-provider').to_upper(), true)
	wlr_deps += logind
endif

subdir('protocol')
subdir('render')

subdir('backend')
subdir('types')
subdir('util')
subdir('xcursor')
subdir('xwayland')

subdir('include')

wlr_parts += [
	lib_wl_protos,
	lib_wlr_backend,
	lib_wlr_render,
	lib_wlr_types,
	lib_wlr_util,
	lib_wlr_xcursor,
]

wlr_deps += [
	wayland_server,
	wayland_client,
	wayland_egl,
	wayland_protos,
	egl,
	glesv2,
	drm,
	gbm,
	libinput,
	xkbcommon,
	udev,
	pixman,
	math,
]

symbols_file = 'wlroots.syms'
symbols_flag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), symbols_file)
lib_wlr = library(
	meson.project_name(),
	version: '.'.join(so_version),
	link_whole: wlr_parts,
	dependencies: wlr_deps,
	include_directories: wlr_inc,
	install: true,
	link_args : symbols_flag,
	link_depends: symbols_file,
)

wlroots = declare_dependency(
	link_with: lib_wlr,
	dependencies: wlr_deps,
	include_directories: wlr_inc,
)

summary = [
	'',
	'----------------',
	'wlroots @0@'.format(meson.project_version()),
	'',
	'      libcap: @0@'.format(conf_data.get('WLR_HAS_LIBCAP', false)),
	'     systemd: @0@'.format(conf_data.get('WLR_HAS_SYSTEMD', false)),
	'     elogind: @0@'.format(conf_data.get('WLR_HAS_ELOGIND', false)),
	'    xwayland: @0@'.format(conf_data.get('WLR_HAS_XWAYLAND', false)),
	' x11_backend: @0@'.format(conf_data.get('WLR_HAS_X11_BACKEND', false)),
	'   xcb-icccm: @0@'.format(conf_data.get('WLR_HAS_XCB_ICCCM', false)),
	'  xcb-errors: @0@'.format(conf_data.get('WLR_HAS_XCB_ERRORS', false)),
	'----------------',
	''
]
message('\n'.join(summary))

subdir('examples')
subdir('rootston')

pkgconfig = import('pkgconfig')
pkgconfig.generate(
	libraries: lib_wlr,
	version: meson.project_version(),
	filebase: meson.project_name(),
	name: meson.project_name(),
	description: 'Wayland compositor library',
)

git = find_program('git', required: false)
if git.found()
	all_files = run_command(
		git,
		'--git-dir=@0@/.git'.format(meson.current_source_dir()),
		'ls-files',
		':/*.[ch]',
	)
	all_files = files(all_files.stdout().split())

	etags = find_program('etags', required: false)
	if etags.found() and all_files.length() > 0
		custom_target(
			'etags',
			build_by_default: true,
			input: all_files,
			output: 'TAGS',
			command: [etags, '-o', '@OUTPUT@', '@INPUT@'],
		)
	endif

	ctags = find_program('ctags', required: false)
	if ctags.found() and all_files.length() > 0
		custom_target(
			'ctags',
			build_by_default: true,
			input: all_files,
			output: 'tags',
			command: [ctags, '-f', '@OUTPUT@', '@INPUT@'],
		)
	endif
endif
