#!/bin/sh

# Generates a simple GL/EGL extension function loader.
#
# The input is a .txt file, with each function to load on its own line.
# If a line starts with a -, it is optional, and will not cause the loader
# to fail if it can't load the function. You'll need to check if that function
# is NULL before using it.

if [ $# -ne 3 ]; then
	exit 1
fi

SPEC=$1
OUT_C=$2
OUT_H=$3

BASE=$(basename "$SPEC" .txt)
INCLUDE_GUARD=$(printf %s "$OUT_H" | tr -c [:alnum:] _ | tr [:lower:] [:upper:])

DECL=""
DEFN=""
LOADER=""

DECL_FMT='extern %s %s;'
DEFN_FMT='%s %s;'
LOADER_FMT='%s = (%s)eglGetProcAddress("%s");'
CHECK_FMT='if (!%s) {
	wlr_log(WLR_ERROR, "Unable to load %s");
	return false;
}'

while read -r COMMAND; do
	OPTIONAL=0
	FUNC_PTR_FMT='PFN%sPROC'

	case $COMMAND in
	-*)
		OPTIONAL=1
		;;
	esac

	case $COMMAND in
	*WL)
		FUNC_PTR_FMT='PFN%s'
		;;
	esac

	COMMAND=${COMMAND#-}
	FUNC_PTR=$(printf "$FUNC_PTR_FMT" "$COMMAND" | tr [:lower:] [:upper:])

	DECL="$DECL$(printf "\n$DECL_FMT" "$FUNC_PTR" "$COMMAND")"
	DEFN="$DEFN$(printf "\n$DEFN_FMT" "$FUNC_PTR" "$COMMAND")"
	LOADER="$LOADER$(printf "\n$LOADER_FMT" "$COMMAND" "$FUNC_PTR" "$COMMAND")"

	if [ $OPTIONAL -eq 0 ]; then
		LOADER="$LOADER$(printf "\n$CHECK_FMT" "$COMMAND" "$COMMAND")"
	fi
done < $SPEC

cat > $OUT_H << EOF
#ifndef $INCLUDE_GUARD
#define $INCLUDE_GUARD

#include <stdbool.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

bool load_$BASE(void);
$DECL

#endif
EOF

cat > $OUT_C << EOF
#include <wlr/util/log.h>
#include "$OUT_H"
$DEFN

bool load_$BASE(void) {
    static bool done = false;
    if (done) {
        return true;
    }
$LOADER

    done = true;
    return true;
}
EOF
