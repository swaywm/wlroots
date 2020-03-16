#include <wlr/util/log.h>
#include "render/sync_file.h"

#ifdef WLR_HAS_LINUX_SYNC_FILE

#include <linux/sync_file.h>
#include <sys/ioctl.h>

bool fd_is_sync_file(int fd) {
	struct sync_file_info info = {0};
	if (ioctl(fd, SYNC_IOC_FILE_INFO, &info) < 0) {
		return false;
	}
	return info.num_fences > 0;
}

int sync_file_merge(int fd1, int fd2) {
	// The kernel will automatically prune signalled fences
	struct sync_merge_data merge_data = { .fd2 = fd2 };
	if (ioctl(fd1, SYNC_IOC_MERGE, &merge_data) < 0) {
		wlr_log_errno(WLR_ERROR, "ioctl(SYNC_IOC_MERGE) failed");
		return -1;
	}

	return merge_data.fence;
}

#else

bool fd_is_sync_file(int fd) {
	return false;
}

int sync_file_merge(int fd1, int fd2) {
	wlr_log(WLR_ERROR, "sync_file support is unavailable");
	return -1;
}

#endif
