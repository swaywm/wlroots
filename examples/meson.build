lib_shared = static_library('shared',
    ['shared.c', 'cat.c'],
    dependencies: dep_wlr)

executable('simple', 'simple.c', dependencies: dep_wlr, link_with: lib_shared)
executable('rotation', 'rotation.c', dependencies: dep_wlr, link_with: lib_shared)
executable('pointer', 'pointer.c', dependencies: dep_wlr, link_with: lib_shared)
executable('touch', 'touch.c', dependencies: dep_wlr, link_with: lib_shared)
executable('tablet', 'tablet.c', dependencies: dep_wlr, link_with: lib_shared)

compositor_src = [
    'compositor/main.c',
    'compositor/wl_compositor.c',
    'compositor/wl_shell.c'
]

executable('compositor', compositor_src, dependencies: dep_wlr, link_with: lib_shared)
