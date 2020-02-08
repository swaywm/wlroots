#include "util/uuid.h"

#include <uuid.h>

#ifdef HAS_LIBUUID
int generate_uuid(char out[static 37]) {
	uuid_t uuid;
	uuid_generate_random(uuid);
	uuid_unparse(uuid, out);
	return 0;
}
#else
#include <string.h>
#include <stdlib.h>

int generate_uuid(char out[static 37]) {
	uuid_t uuid;
	uint32_t status;
	uuid_create(&uuid, &status);
	if (status != uuid_s_ok) {
		return 1;
	}
	char *str;
	uuid_to_string(&uuid, &str, &status);
	if (status != uuid_s_ok) {
		return 1;
	}
	strcpy(out, str);
	free(str);
	return 0;
}
#endif
