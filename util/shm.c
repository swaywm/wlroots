#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wlr/config.h>
#include "util/shm.h"

static void randname(char *buf) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long r = ts.tv_nsec;
	for (int i = 0; i < 6; ++i) {
		buf[i] = 'A'+(r&15)+(r&16)*2;
		r >>= 5;
	}
}

int create_shm_file(void) {
	int retries = 100;
	do {
		char name[] = "/wlroots-XXXXXX";
		randname(name + strlen(name) - 6);

		--retries;
		// CLOEXEC is guaranteed to be set by shm_open
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
			return fd;
		}
	} while (retries > 0 && errno == EEXIST);

	return -1;
}

int allocate_shm_file(size_t size) {
	int fd = create_shm_file();
	if (fd < 0) {
		return -1;
	}

	int ret;
	do {
		ret = ftruncate(fd, size);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	return fd;
}
