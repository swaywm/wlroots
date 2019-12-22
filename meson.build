project(
	'wlroots',
	'c',
	version: '0.8.1',
	license: 'MIT',
	meson_version: '>=0.51.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

# Format of so_version is CURRENT, REVISION, AGE.
# See: https://autotools.io/libtool/version.html
# for a reference about clean library versioning.
so_version = ['3', '6', '1']

add_project_arguments([
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
	'-Wimplicit-fallthrough=2',
	'-Wendif-labels',
	'-Wstrict-aliasing=2',
	'-Woverflow',
	'-Wmissing-prototypes',

	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
]), language: 'c')

# Compute the relative path used by compiler invocations.
source_root = meson.current_source_dir().split('/')
build_root = meson.build_root().split('/')
relative_dir_parts = []
i = 0
in_prefix = true
foreach p : build_root
	if i >= source_root.length() or not in_prefix or p != source_root[i]
		in_prefix = false
		relative_dir_parts += '..'
	endif
	i += 1
endforeach
i = 0
in_prefix = true
foreach p : source_root
	if i >= build_root.length() or not in_prefix or build_root[i] != p
		in_prefix = false
		relative_dir_parts += p
	endif
	i += 1
endforeach
relative_dir = join_paths(relative_dir_parts) + '/'

# Strip relative path prefixes from the code if possible, otherwise hide them.
if cc.has_argument('-fmacro-prefix-map=/prefix/to/hide=')
	add_project_arguments(
		'-fmacro-prefix-map=@0@='.format(relative_dir),
		language: 'c',
	)
else
	add_project_arguments(
		'-DWLR_REL_SRC_DIR="@0@"'.format(relative_dir),
		language: 'c',
	)
endif

conf_data = configuration_data()
conf_data.set10('WLR_HAS_LIBCAP', false)
conf_data.set10('WLR_HAS_SYSTEMD', false)
conf_data.set10('WLR_HAS_ELOGIND', false)
conf_data.set10('WLR_HAS_RDP_BACKEND', false)
conf_data.set10('WLR_HAS_X11_BACKEND', false)
conf_data.set10('WLR_HAS_XWAYLAND', false)
conf_data.set10('WLR_HAS_XCB_ERRORS', false)
conf_data.set10('WLR_HAS_XCB_ICCCM', false)

# Clang complains about some zeroed initializer lists (= {0}), even though they
# are valid
if cc.get_id() == 'clang'
	add_project_arguments('-Wno-missing-field-initializers', language: 'c')
	add_project_arguments('-Wno-missing-braces', language: 'c')
endif

wayland_server = dependency('wayland-server', version: '>=1.16')
wayland_client = dependency('wayland-client')
wayland_egl = dependency('wayland-egl')
wayland_protos = dependency('wayland-protocols', version: '>=1.17')
egl = dependency('egl')
glesv2 = dependency('glesv2')
drm = dependency('libdrm', version: '>=2.4.95')
gbm = dependency('gbm', version: '>=17.1.0')
libinput = dependency('libinput', version: '>=1.9.0')
xkbcommon = dependency('xkbcommon')
udev = dependency('libudev')
pixman = dependency('pixman-1')
math = cc.find_library('m')
rt = cc.find_library('rt')

wlr_files = []
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
	math,
	rt,
]

libinput_ver = libinput.version().split('.')
add_project_arguments([
	'-DLIBINPUT_MAJOR=' + libinput_ver[0],
	'-DLIBINPUT_MINOR=' + libinput_ver[1],
	'-DLIBINPUT_PATCH=' + libinput_ver[2],
], language: 'c')

subdir('protocol')
subdir('render')

subdir('backend')
subdir('types')
subdir('util')
subdir('xcursor')
subdir('xwayland')

subdir('include')

wlr_inc = include_directories('.', 'include')
proto_inc = include_directories('protocol')

symbols_file = 'wlroots.syms'
symbols_flag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), symbols_file)
lib_wlr = library(
	meson.project_name(), wlr_files,
	version: '.'.join(so_version),
	dependencies: wlr_deps,
	include_directories: [wlr_inc, proto_inc],
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
	' rdp_backend: @0@'.format(conf_data.get('WLR_HAS_RDP_BACKEND', false)),
	' x11_backend: @0@'.format(conf_data.get('WLR_HAS_X11_BACKEND', false)),
	'   xcb-icccm: @0@'.format(conf_data.get('WLR_HAS_XCB_ICCCM', false)),
	'  xcb-errors: @0@'.format(conf_data.get('WLR_HAS_XCB_ERRORS', false)),
	'----------------',
	''
]
message('\n'.join(summary))

subdir('examples')

pkgconfig = import('pkgconfig')
pkgconfig.generate(lib_wlr,
	version: meson.project_version(),
	filebase: meson.project_name(),
	name: meson.project_name(),
	description: 'Wayland compositor library',
)
