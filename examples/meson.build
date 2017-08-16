lib_shared = static_library('shared',
  ['shared.c', 'cat.c'],
  dependencies: wlroots)

executable('simple', 'simple.c', dependencies: wlroots, link_with: lib_shared)
executable('rotation', 'rotation.c', dependencies: wlroots, link_with: lib_shared)
executable('pointer', 'pointer.c', dependencies: wlroots, link_with: lib_shared)
executable('touch', 'touch.c', dependencies: wlroots, link_with: lib_shared)
executable('tablet', 'tablet.c', dependencies: wlroots, link_with: lib_shared)
executable('output-layout', 'output-layout.c', dependencies: wlroots, link_with: lib_shared)

compositor_src = [
  'compositor/main.c',
  'compositor/wl_compositor.c',
]

executable('compositor', compositor_src,
  dependencies: wlroots,
  link_with: lib_shared)
