#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/types.h>
#include <wlr/common/list.h>
#include "backend/libinput.h"
#include "common/log.h"
#include "types.h"

struct wlr_touch *wlr_libinput_touch_create(
		struct libinput_device *device) {
	assert(device);
	return wlr_touch_create(NULL, NULL);
}
