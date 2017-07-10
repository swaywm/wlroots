wlr_files += files(
    'direct-ipc.c',
    'direct.c',
    'session.c',
)

if dep_systemd.found()
    wlr_files += files('logind.c')
endif
