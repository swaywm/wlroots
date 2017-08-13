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

wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_egl  = dependency('wayland-egl')
wayland_protos = dependency('wayland-protocols')
egl      = dependency('egl')
glesv2     = dependency('glesv2')
drm      = dependency('libdrm')
gbm      = dependency('gbm')
libinput     = dependency('libinput')
xkbcommon    = dependency('xkbcommon')
udev       = dependency('libudev')
pixman     = dependency('pixman-1')
libcap     = dependency('libcap', required: false)
systemd    = dependency('libsystemd', required: false)
math       = cc.find_library('m', required: false)

if libcap.found()
  add_project_arguments('-DHAS_LIBCAP', language: 'c')
endif

if systemd.found()
  add_project_arguments('-DHAS_SYSTEMD', language: 'c')
endif

subdir('protocol')
subdir('backend')
subdir('render')
subdir('types')
subdir('util')
subdir('xcursor')

_wlr_deps = [
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
  libcap,
  systemd,
  math,
]

lib_wlr = library('wlroots', files('dummy.c'),
  link_whole: [
    lib_wl_protos,
    lib_wlr_backend,
    lib_wlr_render,
    lib_wlr_types,
    lib_wlr_util,
    lib_wlr_xcursor,
  ],
  dependencies: _wlr_deps,
  include_directories: wlr_inc)

wlroots = declare_dependency(link_with: lib_wlr,
  dependencies: _wlr_deps,
  include_directories: wlr_inc)

subdir('examples')
