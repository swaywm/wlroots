#ifndef RENDER_SYNC_FILE_H
#define RENDER_SYNC_FILE_H

#include <stdbool.h>

bool fd_is_sync_file(int fd);
int sync_file_merge(int fd1, int fd2);

#endif
