#ifndef UTIL_DEFS_H
#define UTIL_DEFS_H

#ifdef __GNUC__
#define WLR_API __attribute__((visibility("default")))
#else
#define WLR_API
#endif

#endif
