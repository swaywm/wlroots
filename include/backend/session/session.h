#ifndef BACKEND_SESSION_SESSION_H
#define BACKEND_SESSION_SESSION_H

struct wlr_session;

void session_init(struct wlr_session *session);
int session_try_add_gpu(struct wlr_session *session, struct udev_device *udev_dev);

#endif
