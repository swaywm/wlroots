wlr_files += files('backend.c')

backends = get_option('backends')

foreach backend : ['drm', 'noop', 'libinput', 'x11']
  if backend in backends or 'auto' in backends
    subdir(backend)
  endif
endforeach

subdir('headless')
subdir('multi')
subdir('session')
subdir('wayland')
