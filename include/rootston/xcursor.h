#ifndef _ROOTSTON_XCURSOR_H
#define _ROOTSTON_XCURSOR_H

#include <stdint.h>

#define ROOTS_XCURSOR_SIZE 16

#define ROOTS_XCURSOR_DEFAULT "left_ptr"
#define ROOTS_XCURSOR_MOVE "grabbing"
#define ROOTS_XCURSOR_ROTATE "grabbing"

const char *roots_xcursor_get_resize_name(uint32_t edges);

#endif
