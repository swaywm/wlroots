doxygen = find_program('doxygen', required: false)

if doxygen.found()
  doxy_conf = configuration_data()
  doxy_conf.set('VERSION', meson.project_version())
  doxy_conf.set('OUTPUT_DIRECTORY', meson.build_root())
  doxy_conf.set('INPUT', meson.source_root())

  if find_program('dot', required: false).found()
    doxy_conf.set('HAVE_DOT', 'YES')
  else
    doxy_conf.set('HAVE_DOT', 'NO')
  endif

  doxy_conf.set('HTML_HEADER', join_paths(meson.source_root(), 'man', 'header.html'))
  doxy_conf.set('HTML_FOOTER', join_paths(meson.source_root(), 'man', 'footer.html'))
  doxy_conf.set('HTML_EXTRA_STYLESHEET', join_paths(meson.source_root(), 'man', 'doxy.css'))

  man_conf = doxy_conf
  man_conf.set('GENERATE_MAN', 'YES')
  man_conf.set('GENERATE_HTML', 'NO')

  config = configure_file(
    input: 'Doxyfile.in',
    output: 'Doxyfile.man',
    configuration: man_conf,
    install: false
  )

  html_conf = doxy_conf
  html_conf.set('GENERATE_MAN', 'NO')
  html_conf.set('GENERATE_HTML', 'YES')

  config = configure_file(
    input: 'Doxyfile.in',
    output: 'Doxyfile.html',
    configuration: html_conf,
    install: false
  )

  custom_target(
    'man',
    output: 'man3',
    command: [doxygen, config],
    install: true,
    install_dir: get_option('mandir'),
    build_by_default: get_option('man')
  )

  custom_target(
    'html',
    output: 'html',
    command: [doxygen, config],
    install: false,
    build_by_default: get_option('html')
  )
endif