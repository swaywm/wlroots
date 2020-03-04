prefix = get_option('prefix')
datadir = get_option('datadir')

add_project_arguments('-DDATADIR="@0@"'.format(join_paths(prefix, datadir)), language : 'c')

wlr_files += files(
	'wlr_xcursor.c',
	'xcursor.c',
)
