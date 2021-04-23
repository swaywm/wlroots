#include <uuid.h>
#include "util/uuid.h"

#if HAS_LIBUUID
bool generate_uuid(char out[static 37]) {
	uuid_t uuid;
	uuid_generate_random(uuid);
	uuid_unparse(uuid, out);
	return true;
}
#else
#include <assert.h>
#include <string.h>
#include <stdlib.h>

bool generate_uuid(char out[static 37]) {
	uuid_t uuid;
	uint32_t status;
	uuid_create(&uuid, &status);
	if (status != uuid_s_ok) {
		return false;
	}
	char *str;
	uuid_to_string(&uuid, &str, &status);
	if (status != uuid_s_ok) {
		return false;
	}

	assert(strlen(str) + 1 == 37);
	memcpy(out, str, 37);
	free(str);
	return true;
}
#endif
