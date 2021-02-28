project(
	'wlroots',
	'c',
	version: '0.12.0',
	license: 'MIT',
	meson_version: '>=0.56.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

# When doing a major or minor release, *always* increase soversion. This isn't
# necessary for bugfix releases. Increasing soversion is required because
# wlroots never guarantees ABI stability -- only API stability is guaranteed
# between minor releases.
soversion = 7

add_project_arguments([
	'-DWLR_USE_UNSTABLE',
	# Allow clients to die on their own, be not so strict
	'-DWLR_RELAXED_CLIENT_HANDLING'
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
	'-Walloca',

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

features = {
	'systemd': false,
	'elogind': false,
	'libseat': false,
	'x11-backend': false,
	'xwayland': false,
	'xcb-errors': false,
	'xcb-icccm': false,
	'xdg-foreign': false,
}

wayland_server = dependency('wayland-server', version: '>=1.19')
wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols', version: '>=1.17')
egl = dependency('egl')
glesv2 = dependency('glesv2')
drm = dependency('libdrm', version: '>=2.4.95')
gbm = dependency('gbm', version: '>=17.1.0')
libinput = dependency('libinput', version: '>=1.14.0')
xkbcommon = dependency('xkbcommon')
udev = dependency('libudev')
pixman = dependency('pixman-1')
math = cc.find_library('m')
rt = cc.find_library('rt')
dlfcn = cc.find_library('dl') # for libnvidia-egl-wayland.so.1 dynamic loading

if not get_option('xdg-foreign').disabled()
	uuid = dependency('uuid', required: false)
	uuid_create = cc.has_function('uuid_create')
	if uuid.found() or uuid_create
		features += { 'xdg-foreign': true }
	elif get_option('xdg-foreign').enabled()
		error('Missing dependency uuid and uuid_create function not available ' +
				'cannot build with xdg-foreign support')
	endif
endif

wlr_files = []
wlr_deps = [
	wayland_server,
	wayland_client,
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
	dlfcn
]

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
	soversion: soversion,
	dependencies: wlr_deps,
	include_directories: [wlr_inc, proto_inc],
	install: true,
	link_args: symbols_flag,
	link_depends: symbols_file,
)

wlr_vars = {}
foreach name, have : features
	wlr_vars += { 'have_' + name.underscorify(): have.to_string() }
endforeach

wlroots = declare_dependency(
	link_with: lib_wlr,
	dependencies: wlr_deps,
	include_directories: wlr_inc,
	variables: wlr_vars,
)

meson.override_dependency('wlroots', wlroots)

summary(features, bool_yn: true)

if get_option('examples')
	subdir('examples')
endif

pkgconfig = import('pkgconfig')
pkgconfig.generate(lib_wlr,
	version: meson.project_version(),
	filebase: meson.project_name(),
	name: meson.project_name(),
	description: 'Wayland compositor library',
	variables: wlr_vars,
)
