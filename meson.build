project('wlroots', 'c',
    license: 'MIT',
    default_options: 'c_std=c11')

add_project_arguments('-Wall', '-Wextra', '-Wno-unused-parameter', '-Werror', language: 'c')
add_project_arguments('-DWLR_SRC_DIR="@0@"'.format(meson.source_root()), language: 'c')
add_project_arguments('-I@0@/include'.format(meson.source_root()), language: 'c')

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
    dep_libcap,
    dep_systemd,
    dep_math,
]

wlr_src = [
    'backend/wayland/backend.c',
    'backend/wayland/registry.c',
    'backend/wayland/wl_seat.c',
    'backend/wayland/output.c',
    'backend/drm/backend.c',
    'backend/drm/drm.c',
    'backend/backend.c',
    'backend/libinput/backend.c',
    'backend/libinput/events.c',
    'backend/libinput/keyboard.c',
    'backend/libinput/pointer.c',
    'backend/libinput/tablet_pad.c',
    'backend/libinput/tablet_tool.c',
    'backend/libinput/touch.c',
    'backend/multi/backend.c',
    'backend/udev.c',
    'backend/egl.c',
    'session/direct-ipc.c',
    'session/direct.c',
    'session/session.c',
    'types/wlr_input_device.c',
    'types/wlr_keyboard.c',
    'types/wlr_pointer.c',
    'types/wlr_tablet_pad.c',
    'types/wlr_tablet_tool.c',
    'types/wlr_touch.c',
    'types/wlr_output.c',
    'render/matrix.c',
    'render/gles2/pixel_format.c',
    'render/gles2/renderer.c',
    'render/gles2/shaders.c',
    'render/gles2/surface.c',
    'render/gles2/util.c',
    'render/wlr_renderer.c',
    'render/wlr_surface.c',
    'util/list.c',
    'util/log.c',
]

if dep_libcap.found()
    add_project_arguments('-DHAS_LIBCAP', language: 'c')
endif

if dep_systemd.found()
    wlr_src = wlr_src + ['session/logind.c']
    add_project_arguments('-DHAS_SYSTEMD', language: 'c')
endif

lib_wlr = library('wlroots', wlr_src,
    dependencies: all_deps)
dep_wlr = declare_dependency(link_with: lib_wlr,
    dependencies: all_deps)

lib_shared = static_library('shared',
    ['examples/shared.c', 'examples/cat.c'],
    dependencies: dep_wlr)

all_libs = [
    lib_shared,
    lib_wlr,
]

executable('simple', 'examples/simple.c', link_with: all_libs)
executable('rotation', 'examples/rotation.c', link_with: all_libs)
executable('pointer', 'examples/pointer.c', link_with: all_libs)
executable('touch', 'examples/touch.c', link_with: all_libs)
executable('tablet', 'examples/tablet.c', link_with: all_libs)

compositor_src = [
    'examples/compositor/main.c',
    'examples/compositor/wl_compositor.c',
    'examples/compositor/wl_shell.c',
]

executable('compositor', compositor_src, link_with: all_libs)
