#define _XOPEN_SOURCE 500
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wayland-client.h>
#include "backend/wayland.h"
#include "common/log.h"

// create pointer
// static struct pointer_create
// destroy pointer

// handle surface enter
static void pointer_handle_enter () {

};

// handle surface leave
static void pointer_handle_exit () {
};

// handle motion
static void pointer_handle_motion () {
};

// handle button
static void pointer_handle_button () {
};

// handle axis
static void pointer_handle_axis () {
};

// handle surface death
//static void pointer_handle_surface_death () {
//};

// handle client cursor
//static void pointer_handle_client_cursor () {
//};

const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_exit,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
};
