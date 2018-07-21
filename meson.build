project(
	'wlroots',
	'c',
	version: '0.0.1',
	license: 'MIT',
	meson_version: '>=0.44.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

# Format of so_version is CURRENT, REVISION, AGE.
# See: https://autotools.io/libtool/version.html
# for a reference about clean library versioning.
so_version = ['0', '0', '0']

add_project_arguments('-Wno-unused-parameter', language: 'c')
add_project_arguments(
	'-DWLR_SRC_DIR="@0@"'.format(meson.source_root()),
	language: 'c',
)
add_project_arguments(
	'-I@0@'.format(meson.build_root()),
	language: 'c',
)
add_project_arguments(
	'-include@0@'.format(join_paths(meson.build_root(), 'include', 'wlr', 'config.h')),
	language: 'c',
)
add_project_link_arguments(
	'-Wl,-rpath,@0@'.format(meson.build_root()),
	language: 'c',
)

conf_data = configuration_data()

wlr_inc = include_directories('include')

cc = meson.get_compiler('c')

# Clang complains about some zeroed initializer lists (= {0}), even though they
# are valid
if cc.get_id() == 'clang'
	add_project_arguments('-Wno-missing-field-initializers', language: 'c')
	add_project_arguments('-Wno-missing-braces', language: 'c')
endif

# Avoid wl_buffer deprecation warnings
add_project_arguments('-DWL_HIDE_DEPRECATED', language: 'c')

wayland_server = dependency('wayland-server', version: '>=1.15')
wayland_client = dependency('wayland-client')
wayland_egl    = dependency('wayland-egl')
wayland_protos = dependency('wayland-protocols', version: '>=1.14')
egl            = dependency('egl')
glesv2         = dependency('glesv2')
drm            = dependency('libdrm')
gbm            = dependency('gbm', version: '>=17.1.0')
libinput       = dependency('libinput', version: '>=1.7.0')
xkbcommon      = dependency('xkbcommon')
udev           = dependency('libudev')
pixman         = dependency('pixman-1')
libcap         = dependency('libcap', required: get_option('enable-libcap') == 'true')
systemd        = dependency('libsystemd', required: get_option('enable-systemd') == 'true')
elogind        = dependency('libelogind', required: get_option('enable-elogind') == 'true')
math           = cc.find_library('m', required: false)

exclude_headers = []
wlr_parts = []
wlr_deps = []

if libcap.found() and get_option('enable-libcap') != 'false'
	conf_data.set('WLR_HAS_LIBCAP', true)
	wlr_deps += libcap
endif

if systemd.found() and get_option('enable-systemd') != 'false'
	conf_data.set('WLR_HAS_SYSTEMD', true)
	wlr_deps += systemd
endif

if elogind.found() and get_option('enable-elogind') != 'false'
	conf_data.set('WLR_HAS_ELOGIND', true)
endif

if get_option('enable-x11_backend') or get_option('enable-xwayland')
	xcb            = dependency('xcb')
	xcb_composite  = dependency('xcb-composite')
	xcb_xfixes     = dependency('xcb-xfixes')
	xcb_image      = dependency('xcb-image')
	xcb_render     = dependency('xcb-render')
	x11_xcb        = dependency('x11-xcb')

	xcb_icccm      = dependency('xcb-icccm', required: false)
	xcb_xkb        = dependency('xcb-xkb', required: false)
	xcb_errors     = dependency('xcb-errors', required: get_option('enable-xcb_errors') == 'true')

	if xcb_icccm.found()
		conf_data.set('WLR_HAS_XCB_ICCCM', true)
	endif

	if xcb_xkb.found()
		conf_data.set('WLR_HAS_XCB_XKB', true)
	endif

	if xcb_errors.found() and get_option('enable-xcb_errors') != 'false'
		conf_data.set('WLR_HAS_XCB_ERRORS', true)
	endif

	wlr_deps += [
		xcb,
		xcb_composite,
		x11_xcb,
	]
else
	add_project_arguments('-DMESA_EGL_NO_X11_HEADERS', language: 'c')
endif

if get_option('enable-x11_backend')
	conf_data.set('WLR_HAS_X11_BACKEND', true)
endif

if get_option('enable-xwayland')
	subdir('xwayland')
	wlr_parts += [lib_wlr_xwayland]
	conf_data.set('WLR_HAS_XWAYLAND', true)
else
	exclude_headers += 'xwayland.h'
endif

if cc.has_header_symbol('fcntl.h', 'posix_fallocate', prefix: '#define _POSIX_C_SOURCE 200112L')
	conf_data.set('WLR_HAS_POSIX_FALLOCATE', true)
endif

includedir = get_option('includedir')
exclude_headers += 'meson.build'
install_subdir('include/wlr', install_dir: includedir, exclude_files: exclude_headers)


subdir('include')
subdir('protocol')
subdir('render')
subdir('backend')
subdir('types')
subdir('util')
subdir('xcursor')

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


if get_option('enable-rootston')
	subdir('rootston')
endif

if get_option('enable-examples')
	subdir('examples')
endif

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
		['--git-dir=@0@/.git'.format(meson.current_source_dir()),
		 'ls-files',
		 ':/*.[ch]'])
	all_files = files(all_files.stdout().split())

	etags = find_program('etags', required: false)
	if etags.found() and all_files.length() > 0
		custom_target('etags',
			build_by_default: true,
			input: all_files,
			output: 'TAGS',
			command: [etags.path(), '-o', 'TAGS'] + all_files)
	endif

	ctags = find_program('ctags', required: false)
	if ctags.found() and all_files.length() > 0
		custom_target('ctags',
			build_by_default: true,
			input: all_files,
			output: 'tags',
			command: [ctags.path(), '-o', 'tags'] + all_files)
	endif
endif
