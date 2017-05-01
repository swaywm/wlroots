#ifndef UDEV_H
#define UDEV_H

bool otd_udev_start(struct otd *otd);
void otd_udev_finish(struct otd *otd);
void otd_udev_find_gpu(struct otd *otd);
void otd_udev_event(struct otd *otd);

#endif
