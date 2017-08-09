project('wlroots', 'c',
    license: 'MIT',
    default_options: 'c_std=c11')

add_project_arguments('-Wall', '-Wextra', '-Wno-unused-parameter', '-Werror', language: 'c')
add_project_arguments('-DWLR_SRC_DIR="@0@"'.format(meson.source_root()), language: 'c')
add_project_link_arguments('-Wl,-rpath,@0@'.format(meson.build_root()), language: 'c')

wlr_inc = include_directories('include')

#add_project_arguments('-flto', language: 'c')
#add_project_link_arguments('-flto', language: 'c')

cc = meson.get_compiler('c')

# Clang complains about some zeroed initialiser lists (= {0}), even though they are valid
if cc.get_id() == 'clang'
    add_project_arguments('-Wno-missing-field-initializers', language: 'c')
    add_project_arguments('-Wno-missing-braces', language: 'c')
endif

dep_wayland_server = dependency('wayland-server')
dep_wayland_client = dependency('wayland-client')
dep_wayland_egl    = dependency('wayland-egl')
dep_wayland_proto  = dependency('wayland-protocols')
dep_egl            = dependency('egl')
dep_glesv2         = dependency('glesv2')
dep_drm            = dependency('libdrm')
dep_gbm            = dependency('gbm')
dep_libinput       = dependency('libinput')
dep_xkbcommon      = dependency('xkbcommon')
dep_udev           = dependency('libudev')
dep_pixman           = dependency('pixman-1')
dep_libcap         = dependency('libcap', required: false)
dep_systemd        = dependency('libsystemd', required: false)
dep_math           = cc.find_library('m', required: false)

all_deps = [
    dep_wayland_server,
    dep_wayland_client,
    dep_wayland_egl,
    dep_wayland_proto,
    dep_egl,
    dep_glesv2,
    dep_drm,
    dep_gbm,
    dep_libinput,
    dep_xkbcommon,
    dep_udev,
    dep_pixman,
    dep_libcap,
    dep_systemd,
    dep_math,
]

if dep_libcap.found()
    add_project_arguments('-DHAS_LIBCAP', language: 'c')
endif

if dep_systemd.found()
    add_project_arguments('-DHAS_SYSTEMD', language: 'c')
endif

wlr_files = []

subdir('backend')
subdir('render')
subdir('types')
subdir('util')
subdir('xcursor')

lib_wlr = library('wlroots', wlr_files,
	dependencies: all_deps,
	include_directories: wlr_inc)
dep_wlr = declare_dependency(link_with: lib_wlr,
	dependencies: all_deps,
	include_directories: wlr_inc)

subdir('examples')
