lib_shared = static_library('shared',
    ['shared.c', 'cat.c'],
    dependencies: dep_wlr)

all_libs = [
    lib_shared,
    lib_wlr,
]

executable('simple', 'simple.c', link_with: all_libs)
executable('rotation', 'rotation.c', link_with: all_libs)
executable('pointer', 'pointer.c', link_with: all_libs)
executable('touch', 'touch.c', link_with: all_libs)
executable('tablet', 'tablet.c', link_with: all_libs)

compositor_src = [
    'compositor/main.c',
    'compositor/wl_compositor.c',
    'compositor/wl_shell.c',
]

executable('compositor', compositor_src, link_with: all_libs)
