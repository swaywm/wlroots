project(
	'wlroots',
	'c',
	version: '0.0.1',
	license: 'MIT',
	meson_version: '>=0.43.0',
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

wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_egl    = dependency('wayland-egl')
wayland_protos = dependency('wayland-protocols')
egl            = dependency('egl')
glesv2         = dependency('glesv2')
drm            = dependency('libdrm')
gbm            = dependency('gbm', version: '>=17.1.0')
libinput       = dependency('libinput', version: '>=1.7.0')
xkbcommon      = dependency('xkbcommon')
udev           = dependency('libudev')
pixman         = dependency('pixman-1')
xcb            = dependency('xcb')
xcb_composite  = dependency('xcb-composite')
xcb_xfixes     = dependency('xcb-xfixes')
xcb_image      = dependency('xcb-image')
xcb_render     = dependency('xcb-render')
xcb_icccm      = dependency('xcb-icccm', required: false)
x11_xcb        = dependency('x11-xcb')
libcap         = dependency('libcap', required: get_option('enable_libcap') == 'true')
systemd        = dependency('libsystemd', required: get_option('enable_systemd') == 'true')
elogind        = dependency('libelogind', required: get_option('enable_elogind') == 'true')
math           = cc.find_library('m', required: false)

exclude_headers = []
wlr_parts = []
wlr_deps = []

if xcb_icccm.found()
	conf_data.set('WLR_HAS_XCB_ICCCM', true)
endif

if libcap.found() and get_option('enable_libcap') != 'false'
	conf_data.set('WLR_HAS_LIBCAP', true)
	wlr_deps += libcap
endif

if systemd.found() and get_option('enable_systemd') != 'false'
	conf_data.set('WLR_HAS_SYSTEMD', true)
	wlr_deps += systemd
endif

if elogind.found() and get_option('enable_elogind') != 'false'
	conf_data.set('WLR_HAS_ELOGIND', true)
endif

if get_option('enable_xwayland')
	subdir('xwayland')
	wlr_parts += [lib_wlr_xwayland]
	conf_data.set('WLR_HAS_XWAYLAND', true)
else
	exclude_headers += 'xwayland.h'
	exclude_headers += 'xwm.h'
endif
exclude_headers += 'meson.build'
install_subdir('include/wlr', install_dir: 'include', exclude_files: exclude_headers)


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
	xcb,
	xcb_composite,
	x11_xcb,
	math,
]

lib_wlr = library(
	meson.project_name(),
	version: '.'.join(so_version),
	link_whole: wlr_parts,
	dependencies: wlr_deps,
	include_directories: wlr_inc,
	install: true,
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
	'   libcap: @0@'.format(conf_data.get('WLR_HAS_LIBCAP', false)),
	'  systemd: @0@'.format(conf_data.get('WLR_HAS_SYSTEMD', false)),
	'  elogind: @0@'.format(conf_data.get('WLR_HAS_ELOGIND', false)),
	' xwayland: @0@'.format(conf_data.get('WLR_HAS_XWAYLAND', false)),
	'----------------',
	''
]
message('\n'.join(summary))


subdir('rootston')
subdir('examples')

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
		['--git-dir=@0@/.git'.format(meson.source_root()),
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
