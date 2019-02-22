#define _POSIX_C_SOURCE 200809L
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include "backend/rdp.h"
#include "util/signal.h"

static BOOL xf_peer_capabilities(freerdp_peer *client) {
	return TRUE;
}

static BOOL xf_peer_post_connect(freerdp_peer *client) {
	return TRUE;
}

static BOOL xf_peer_activate(freerdp_peer *client) {
	struct wlr_rdp_peer_context *context =
		(struct wlr_rdp_peer_context *)client->context;
	struct wlr_rdp_backend *backend = context->backend;
	rdpSettings *settings = client->settings;

	if (!settings->SurfaceCommandsEnabled) {
		wlr_log(WLR_ERROR, "RDP peer does not support SurfaceCommands");
		return FALSE;
	}

	context->output = wlr_rdp_output_create(backend, context,
			(int)settings->DesktopWidth, (int)settings->DesktopHeight);
	if (!context->output) {
		wlr_log(WLR_ERROR, "Failed to allcoate output for RDP peer");
		return FALSE;
	}
	rfx_context_reset(context->rfx_context,
			context->output->wlr_output.width,
			context->output->wlr_output.height);
	nsc_context_reset(context->nsc_context,
			context->output->wlr_output.width,
			context->output->wlr_output.height);

	if (context->flags & RDP_PEER_ACTIVATED) {
		return TRUE;
	}

	context->pointer = wlr_rdp_pointer_create(backend, context);
	if (!context->pointer) {
		wlr_log(WLR_ERROR, "Failed to allocate pointer for RDP peer");
		return FALSE;
	}

	// Use wlroots' software cursors instead of remote cursors
	POINTER_SYSTEM_UPDATE pointer_system;
	rdpPointerUpdate *pointer = client->update->pointer;
	pointer_system.type = SYSPTR_NULL;
	pointer->PointerSystem(client->context, &pointer_system);

	context->keyboard = wlr_rdp_keyboard_create(backend, settings);
	if (!context->keyboard) {
		wlr_log(WLR_ERROR, "Failed to allocate keyboard for RDP peer");
		return FALSE;
	}

	context->flags |= RDP_PEER_ACTIVATED;
	return TRUE;
}

static int xf_suppress_output(rdpContext *context,
		BYTE allow, const RECTANGLE_16 *area) {
	struct wlr_rdp_peer_context *peer_context =
		(struct wlr_rdp_peer_context *)context;
	if (allow) {
		peer_context->flags |= RDP_PEER_OUTPUT_ENABLED;
	} else {
		peer_context->flags &= ~RDP_PEER_OUTPUT_ENABLED;
	}
	return true;
}

static int xf_input_synchronize_event(rdpInput *input, UINT32 flags) {
	struct wlr_rdp_peer_context *context =
		(struct wlr_rdp_peer_context *)input->context;
	wlr_output_damage_whole(&context->output->wlr_output);
	return true;
}

static inline int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

static int xf_input_mouse_event(rdpInput *input,
		UINT16 flags, UINT16 x, UINT16 y) {
	struct wlr_rdp_peer_context *context =
		(struct wlr_rdp_peer_context *)input->context;
	struct wlr_input_device *wlr_device = &context->pointer->wlr_input_device;
	struct wlr_pointer *pointer = wlr_device->pointer;
	bool frame = false;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (flags & PTR_FLAGS_MOVE) {
		struct wlr_event_pointer_motion_absolute event = { 0 };
		event.device = wlr_device;
		event.time_msec = timespec_to_msec(&now);
		event.x = x / (double)context->output->wlr_output.width;
		event.y = y / (double)context->output->wlr_output.height;
		wlr_signal_emit_safe(&pointer->events.motion_absolute, &event);
		frame = true;
	}

	uint32_t button = 0;
	if (flags & PTR_FLAGS_BUTTON1) {
		button = BTN_LEFT;
	} else if (flags & PTR_FLAGS_BUTTON2) {
		button = BTN_RIGHT;
	} else if (flags & PTR_FLAGS_BUTTON3) {
		button = BTN_MIDDLE;
	}

	if (button) {
		struct wlr_event_pointer_button event = { 0 };
		event.device = wlr_device;
		event.time_msec = timespec_to_msec(&now);
		event.button = button;
		event.state = (flags & PTR_FLAGS_DOWN) ?
			WLR_BUTTON_PRESSED : WLR_BUTTON_RELEASED;
		wlr_signal_emit_safe(&pointer->events.button, &event);
		frame = true;
	}

	if (flags & PTR_FLAGS_WHEEL) {
		double value = -(flags & 0xFF) / 120.0;
		if (flags & PTR_FLAGS_WHEEL_NEGATIVE) {
			value = -value;
		}
		struct wlr_event_pointer_axis event = { 0 };
		event.device = &context->pointer->wlr_input_device;
		event.time_msec = timespec_to_msec(&now);
		event.source = WLR_AXIS_SOURCE_WHEEL;
		event.orientation = WLR_AXIS_ORIENTATION_VERTICAL;
		event.delta = value;
		event.delta_discrete = (int32_t)value;
		wlr_signal_emit_safe(&pointer->events.axis, &event);
		frame = true;
	}

	if (frame) {
		wlr_signal_emit_safe(&pointer->events.frame, pointer);
	}

	return true;
}

static int xf_input_extended_mouse_event(
		rdpInput *input, UINT16 flags, UINT16 x, UINT16 y) {
	struct wlr_rdp_peer_context *context =
		(struct wlr_rdp_peer_context *)input->context;
	struct wlr_input_device *wlr_device = &context->pointer->wlr_input_device;
	struct wlr_pointer *pointer = wlr_device->pointer;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	struct wlr_event_pointer_motion_absolute event = { 0 };
	event.device = wlr_device;
	event.time_msec = timespec_to_msec(&now);
	event.x = x / (double)context->output->wlr_output.width;
	event.y = y / (double)context->output->wlr_output.height;
	wlr_signal_emit_safe(&pointer->events.motion_absolute, &event);
	wlr_signal_emit_safe(&pointer->events.frame, pointer);
	return true;
}

static int xf_input_keyboard_event(rdpInput *input, UINT16 flags, UINT16 code) {
	struct wlr_rdp_peer_context *context =
		(struct wlr_rdp_peer_context *)input->context;
	struct wlr_input_device *wlr_device = &context->keyboard->wlr_input_device;
	struct wlr_keyboard *keyboard = wlr_device->keyboard;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (!(context->flags & RDP_PEER_ACTIVATED)) {
		return true;
	}

	bool notify = false;
	enum wlr_key_state state;
	if ((flags & KBD_FLAGS_DOWN)) {
		state = WLR_KEY_PRESSED;
		notify = true;
	} else if ((flags & KBD_FLAGS_RELEASE)) {
		state = WLR_KEY_RELEASED;
		notify = true;
	}

	if (notify) {
		uint32_t full_code = code;
		if (flags & KBD_FLAGS_EXTENDED) {
			full_code |= KBD_FLAGS_EXTENDED;
		}
		uint32_t vk_code = GetVirtualKeyCodeFromVirtualScanCode(full_code, 4);
		if (flags & KBD_FLAGS_EXTENDED) {
			vk_code |= KBDEXT;
		}
		uint32_t scan_code = GetKeycodeFromVirtualKeyCode(
				vk_code, KEYCODE_TYPE_EVDEV);
		struct wlr_event_keyboard_key event = { 0 };
		event.time_msec = timespec_to_msec(&now);
		event.keycode = scan_code - 8;
		event.state = state;
		event.update_state = true;
		wlr_keyboard_notify_key(keyboard, &event);
	}

	return true;
}

static int xf_input_unicode_keyboard_event(rdpInput *input,
		UINT16 flags, UINT16 code) {
	wlr_log(WLR_DEBUG, "Unhandled RDP unicode keyboard event "
			"(flags:0x%X code:0x%X)\n", flags, code);
	return true;
}

static int rdp_client_activity(int fd, uint32_t mask, void *data) {
	freerdp_peer *client = (freerdp_peer *)data;
	if (!client->CheckFileDescriptor(client)) {
		wlr_log(WLR_ERROR,
				"Unable to check client file descriptor for %p", client);
		freerdp_peer_context_free(client);
		freerdp_peer_free(client);
	}
	return 0;
}

static int rdp_peer_context_new(
		freerdp_peer *client, struct wlr_rdp_peer_context *context) {
	context->peer = client;
	context->flags = RDP_PEER_OUTPUT_ENABLED;
	context->rfx_context = rfx_context_new(TRUE);
	if (!context->rfx_context) {
		return false;
	}
	context->rfx_context->mode = RLGR3;
	context->rfx_context->width = client->settings->DesktopWidth;
	context->rfx_context->height = client->settings->DesktopHeight;
	rfx_context_set_pixel_format(context->rfx_context, PIXEL_FORMAT_BGRA32);

	context->nsc_context = nsc_context_new();
	if (!context->nsc_context) {
		rfx_context_free(context->rfx_context);
		return false;
	}

	nsc_context_set_pixel_format(context->nsc_context, PIXEL_FORMAT_BGRA32);

	context->encode_stream = Stream_New(NULL, 65536);
	if (!context->encode_stream) {
		nsc_context_free(context->nsc_context);
		rfx_context_free(context->rfx_context);
		return false;
	}
	return true;
}

static void rdp_peer_context_free(
		freerdp_peer *client, struct wlr_rdp_peer_context *context) {
	if (!context) {
		return;
	}

	for (int i = 0; i < MAX_FREERDP_FDS; ++i) {
		if (context->events[i]) {
			wl_event_source_remove(context->events[i]);
		}
	}

	if (context->flags & RDP_PEER_ACTIVATED) {
		wlr_output_destroy(&context->output->wlr_output);
		wlr_input_device_destroy(&context->pointer->wlr_input_device);
		wlr_input_device_destroy(&context->keyboard->wlr_input_device);
	}

	wl_list_remove(&context->link);
	wlr_output_destroy(&context->output->wlr_output);

	Stream_Free(context->encode_stream, TRUE);
	nsc_context_free(context->nsc_context);
	rfx_context_free(context->rfx_context);
	free(context->rfx_rects);
}

int rdp_peer_init(freerdp_peer *client,
		struct wlr_rdp_backend *backend) {
	client->ContextSize = sizeof(struct wlr_rdp_peer_context);
	client->ContextNew = (psPeerContextNew)rdp_peer_context_new;
	client->ContextFree = (psPeerContextFree)rdp_peer_context_free;
	freerdp_peer_context_new(client);

	struct wlr_rdp_peer_context *peer_context =
		(struct wlr_rdp_peer_context *)client->context;
	peer_context->backend = backend;

	client->settings->CertificateFile = strdup(backend->tls_cert_path);
	client->settings->PrivateKeyFile = strdup(backend->tls_key_path);
	client->settings->NlaSecurity = FALSE;

	if (!client->Initialize(client)) {
		wlr_log(WLR_ERROR, "Failed to initialize FreeRDP peer");
		goto err_init;
	}

	client->settings->OsMajorType = OSMAJORTYPE_UNIX;
	client->settings->OsMinorType = OSMINORTYPE_PSEUDO_XSERVER;
	client->settings->ColorDepth = 32;
	client->settings->RefreshRect = TRUE;
	client->settings->RemoteFxCodec = TRUE;
	client->settings->NSCodec = TRUE;
	client->settings->FrameMarkerCommandEnabled = TRUE;
	client->settings->SurfaceFrameMarkerEnabled = TRUE;

	client->Capabilities = xf_peer_capabilities;
	client->PostConnect = xf_peer_post_connect;
	client->Activate = xf_peer_activate;

	client->update->SuppressOutput = (pSuppressOutput)xf_suppress_output;

	client->input->SynchronizeEvent = xf_input_synchronize_event;
	client->input->MouseEvent = xf_input_mouse_event;
	client->input->ExtendedMouseEvent = xf_input_extended_mouse_event;
	client->input->KeyboardEvent = xf_input_keyboard_event;
	client->input->UnicodeKeyboardEvent = xf_input_unicode_keyboard_event;

	int rcount = 0;
	void *rfds[MAX_FREERDP_FDS];
	if (!client->GetFileDescriptor(client, rfds, &rcount)) {
		wlr_log(WLR_ERROR, "Unable to retrieve client file descriptors");
		goto err_init;
	}
	struct wl_event_loop *event_loop =
		wl_display_get_event_loop(backend->display);
	int i;
	for (i = 0; i < rcount; ++i) {
		int fd = (int)(long)(rfds[i]);
		peer_context->events[i] = wl_event_loop_add_fd(
				event_loop, fd, WL_EVENT_READABLE, rdp_client_activity,
				client);
	}
	for (; i < MAX_FREERDP_FDS; ++i) {
		peer_context->events[i] = NULL;
	}

	wl_list_insert(&backend->clients, &peer_context->link);
	return 0;

err_init:
	client->Close(client);
	return -1;
}
