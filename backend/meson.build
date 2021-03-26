wlr_files += files('backend.c')

backends = get_option('backends')

if 'auto' in backends and get_option('auto_features').disabled()
  warning('The global auto features is set to disabled but we would still enable those backends if their dependencies are met')
endif

foreach backend : ['drm', 'noop', 'libinput', 'x11']
  if backend in backends or 'auto' in backends
    subdir(backend)
  endif
endforeach

subdir('headless')
subdir('multi')
subdir('session')
subdir('wayland')
