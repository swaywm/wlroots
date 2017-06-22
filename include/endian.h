#ifndef _WLR_ENDIAN_H
#define _WLR_ENDIAN_H

// https://stackoverflow.com/a/4240257

#define little_endian() (((union { unsigned x; unsigned char c; }){1}).c)

#endif
