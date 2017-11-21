project(
	'wlroots',
	'c',
	license: 'MIT',
	meson_version: '>=0.43.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

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

wlr_inc = include_directories('include')

cc = meson.get_compiler('c')

# Clang complains about some zeroed initialiser lists (= {0}), even though they
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
libcap         = dependency('libcap', required: false)
systemd        = dependency('libsystemd', required: false)
elogind        = dependency('libelogind', required: false)
math           = cc.find_library('m', required: false)

if xcb_icccm.found()
	add_project_arguments('-DHAS_XCB_ICCCM', language: 'c')
endif

if libcap.found() and get_option('enable_libcap')
	add_project_arguments('-DHAS_LIBCAP', language: 'c')
endif

if systemd.found() and get_option('enable_systemd')
	add_project_arguments('-DHAS_SYSTEMD', language: 'c')
endif

if elogind.found() and get_option('enable_elogind')
	add_project_arguments('-DHAS_ELOGIND', language: 'c')
endif

exclude_files = []
wlr_parts = []
conf_data = configuration_data()
if get_option('enable_xwayland')
	add_project_arguments('-DHAS_XWAYLAND', language: 'c')
	subdir('xwayland')
	wlr_parts += [lib_wlr_xwayland]
	conf_data.set('WLR_HAS_XWAYLAND', true)
else
	exclude_files += ['xwayland.h']
endif
configure_file(output: 'config.h', install_dir: 'include/wlr', configuration: conf_data)
install_subdir('include/wlr', install_dir: 'include', exclude_files: exclude_files)


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

wlr_deps = [
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
	libcap,
	systemd,
	math,
]

lib_wlr = library(
	'wlroots',
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

subdir('rootston')
subdir('examples')

pkgconfig = import('pkgconfig')
pkgconfig.generate(
	libraries: lib_wlr,
	version: '0.0.1',
	filebase: 'wlroots',
	name: 'wlroots',
	description: 'Wayland compositor library',
)
