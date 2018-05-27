#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 199309L
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "wlr-export-dmabuf-unstable-v1-client-protocol.h"

#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/display.h>
#include <libavutil/hwcontext_drm.h>

struct wayland_output {
	struct wl_list link;
	uint32_t id;
	struct wl_output *output;
	char *make;
	char *model;
	int width;
	int height;
	AVRational framerate;
};

struct capture_context {
	AVClass *class; /* For pretty logging */
	struct wl_display *display;
	struct wl_registry *registry;
	struct zwlr_export_dmabuf_manager_v1 *export_manager;

	struct wl_list output_list;

	/* Target */
	struct wl_output *target_output;
	uint32_t target_client;

	/* Main frame callback */
	struct zwlr_export_dmabuf_frame_v1 *frame_callback;

	/* If something happens during capture */
	int err;
	int quit;

	/* FFmpeg specific parts */
	AVFrame *current_frame;
	AVBufferRef *drm_device_ref;
	AVBufferRef *drm_frames_ref;

    AVBufferRef *mapped_device_ref;
    AVBufferRef *mapped_frames_ref;

    AVFormatContext *avf;
    AVCodecContext *avctx;

    int64_t start_pts;

	/* Config */
	enum AVPixelFormat software_format;
	enum AVHWDeviceType hw_device_type;
	AVDictionary *encoder_opts;
	int is_software_encoder;
	char *hardware_device;
	char *out_filename;
	char *encoder_name;
	float out_bitrate;
};

static void output_handle_geometry(void *data, struct wl_output *wl_output,
		int32_t x, int32_t y, int32_t phys_width, int32_t phys_height,
		int32_t subpixel, const char *make, const char *model,
		int32_t transform) {
	struct wayland_output *output = data;
	output->make  = av_strdup(make);
	output->model = av_strdup(model);
}

static void output_handle_mode(void *data, struct wl_output *wl_output,
		uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
	if (flags & WL_OUTPUT_MODE_CURRENT) {
	    struct wayland_output *output = data;
	    output->width     = width;
	    output->height    = height;
	    output->framerate = (AVRational){ refresh, 1000 };
	}
}

static void output_handle_done(void* data, struct wl_output *wl_output) {
	/* Nothing to do */
}

static void output_handle_scale(void* data, struct wl_output *wl_output,
		int32_t factor) {
	/* Nothing to do */
}

static const struct wl_output_listener output_listener = {
	output_handle_geometry,
	output_handle_mode,
	output_handle_done,
	output_handle_scale,
};

static void registry_handle_add(void *data, struct wl_registry *reg,
		uint32_t id, const char *interface, uint32_t ver) {
	struct capture_context *ctx = data;

	if (!strcmp(interface, wl_output_interface.name)) {
		struct wayland_output *output = av_mallocz(sizeof(*output));

		output->id     = id;
		output->output = wl_registry_bind(reg, id, &wl_output_interface, 1);

		wl_output_add_listener(output->output, &output_listener, output);
		wl_list_insert(&ctx->output_list, &output->link);
	}

	if (!strcmp(interface, zwlr_export_dmabuf_manager_v1_interface.name)) {
			ctx->export_manager = wl_registry_bind(reg, id,
					&zwlr_export_dmabuf_manager_v1_interface, 1);
	}
}

static void remove_output(struct wayland_output *out) {
	wl_list_remove(&out->link);
	av_free(out->make);
	av_free(out->model);
	av_free(out);
	return;
}

static struct wayland_output *find_output(struct capture_context *ctx,
		struct wl_output *out, uint32_t id) {
	struct wayland_output *output, *tmp;
	wl_list_for_each_safe(output, tmp, &ctx->output_list, link)
		if ((output->output == out) || (output->id == id))
			return output;
    return NULL;
}

static void registry_handle_remove(void *data, struct wl_registry *reg,
		uint32_t id) {
	remove_output(find_output((struct capture_context *)data, NULL, id));
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_add,
	registry_handle_remove,
};

static void frame_free(void *opaque, uint8_t *data) {
	AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)data;

	for (int i = 0; i < desc->nb_objects; ++i) {
		close(desc->objects[i].fd);
	}

	zwlr_export_dmabuf_frame_v1_destroy(opaque);

	av_free(data);
}

static void frame_start(void *data, struct zwlr_export_dmabuf_frame_v1 *frame,
		uint32_t width, uint32_t height, uint32_t offset_x, uint32_t offset_y,
		uint32_t buffer_flags, uint32_t flags, uint32_t format,
		uint32_t mod_high, uint32_t mod_low, uint32_t num_objects,
		uint32_t num_planes) {
	struct capture_context *ctx = data;
	int err = 0;

	/* Allocate DRM specific struct */
	AVDRMFrameDescriptor *desc = av_mallocz(sizeof(*desc));
	if (!desc) {
		err = AVERROR(ENOMEM);
		goto fail;
	}

	desc->nb_objects = num_objects;
	desc->objects[0].format_modifier = ((uint64_t)mod_high << 32) | mod_low;

	desc->nb_layers = 1;
	desc->layers[0].format = format;
	desc->layers[0].nb_planes = num_planes;

	/* Allocate a frame */
	AVFrame *f = av_frame_alloc();
	if (!f) {
		err = AVERROR(ENOMEM);
		goto fail;
	}

	/* Set base frame properties */
	ctx->current_frame = f;
	f->width  = width;
	f->height = height;
	f->format = AV_PIX_FMT_DRM_PRIME;

	/* Set the frame data to the DRM specific struct */
	f->buf[0] = av_buffer_create((uint8_t*)desc, sizeof(*desc),
			&frame_free, frame, 0);
	if (!f->buf[0]) {
		err = AVERROR(ENOMEM);
		goto fail;
	}

	f->data[0] = (uint8_t*)desc;

	return;

fail:
	ctx->err = err;
	frame_free(frame, (uint8_t *)desc);
}

static void frame_object(void *data, struct zwlr_export_dmabuf_frame_v1 *frame,
		uint32_t index, int32_t fd, uint32_t size) {
	struct capture_context *ctx = data;
	AVFrame *f = ctx->current_frame;
	AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)f->data[0];

	desc->objects[index].fd   = fd;
	desc->objects[index].size = size;
}

static void frame_plane(void *data, struct zwlr_export_dmabuf_frame_v1 *frame,
		uint32_t index, uint32_t object_index,
		uint32_t offset, uint32_t stride) {
	struct capture_context *ctx = data;
	AVFrame *f = ctx->current_frame;
	AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)f->data[0];

	desc->layers[0].planes[index].object_index = object_index;
	desc->layers[0].planes[index].offset       = offset;
	desc->layers[0].planes[index].pitch        = stride;
}

static const uint32_t pixfmt_to_drm_map[] = {
	[AV_PIX_FMT_NV12] = WL_SHM_FORMAT_NV12,
	[AV_PIX_FMT_BGRA] = WL_SHM_FORMAT_ARGB8888,
	[AV_PIX_FMT_BGR0] = WL_SHM_FORMAT_XRGB8888,
	[AV_PIX_FMT_RGBA] = WL_SHM_FORMAT_ABGR8888,
	[AV_PIX_FMT_RGB0] = WL_SHM_FORMAT_XBGR8888,
	[AV_PIX_FMT_ABGR] = WL_SHM_FORMAT_RGBA8888,
	[AV_PIX_FMT_0BGR] = WL_SHM_FORMAT_RGBX8888,
	[AV_PIX_FMT_ARGB] = WL_SHM_FORMAT_BGRA8888,
	[AV_PIX_FMT_0RGB] = WL_SHM_FORMAT_BGRX8888,
};

static enum AVPixelFormat drm_fmt_to_pixfmt(uint32_t fmt) {
	for (enum AVPixelFormat i = 0; i < AV_PIX_FMT_NB; i++) {
		if (pixfmt_to_drm_map[i] == fmt) {
			return i;
		}
	}
	return AV_PIX_FMT_NONE;
}

static int attach_drm_frames_ref(struct capture_context *ctx, AVFrame *f,
		enum AVPixelFormat sw_format) {
	int err = 0;
	AVHWFramesContext *hwfc;

	if (ctx->drm_frames_ref) {
		hwfc = (AVHWFramesContext*)ctx->drm_frames_ref->data;
		if (hwfc->width == f->width && hwfc->height == f->height &&
				hwfc->sw_format == sw_format) {
			goto attach;
		}
		av_buffer_unref(&ctx->drm_frames_ref);
	}

	ctx->drm_frames_ref = av_hwframe_ctx_alloc(ctx->drm_device_ref);
	if (!ctx->drm_frames_ref) {
		err = AVERROR(ENOMEM);
		goto fail;
	}

	hwfc = (AVHWFramesContext*)ctx->drm_frames_ref->data;

	hwfc->format    = f->format;
	hwfc->sw_format = sw_format;
	hwfc->width     = f->width;
	hwfc->height    = f->height;

	err = av_hwframe_ctx_init(ctx->drm_frames_ref);
	if (err) {
		av_log(ctx, AV_LOG_ERROR, "AVHWFramesContext init failed: %s!\n",
				av_err2str(err));
		goto fail;
	}

attach:
	/* Set frame hardware context referencce */
	f->hw_frames_ctx = av_buffer_ref(ctx->drm_frames_ref);
	if (!f->hw_frames_ctx) {
		err = AVERROR(ENOMEM);
		goto fail;
	}

	return 0;

fail:
	av_buffer_unref(&ctx->drm_frames_ref);
	return err;
}

static void register_cb(struct capture_context *ctx);

static void frame_ready(void *data, struct zwlr_export_dmabuf_frame_v1 *frame,
		uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
	struct capture_context *ctx = data;
	AVFrame *f = ctx->current_frame;
	AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)f->data[0];
	int err = 0;

	/* Attach the hardware frame context to the frame */
	err = attach_drm_frames_ref(ctx, f, drm_fmt_to_pixfmt(desc->layers[0].format));
	if (err) {
		goto end;
	}

	AVFrame *mapped_frame = av_frame_alloc();
	if (!mapped_frame) {
		err = AVERROR(ENOMEM);
		goto end;
	}

	AVHWFramesContext *mapped_hwfc;
	mapped_hwfc = (AVHWFramesContext *)ctx->mapped_frames_ref->data;
    mapped_frame->format = mapped_hwfc->format;

	/* Set frame hardware context referencce */
	mapped_frame->hw_frames_ctx = av_buffer_ref(ctx->mapped_frames_ref);
	if (!mapped_frame->hw_frames_ctx) {
		err = AVERROR(ENOMEM);
		goto end;
	}

	err = av_hwframe_map(mapped_frame, f, 0);
	if (err) {
	    av_log(ctx, AV_LOG_ERROR, "Error mapping: %s!\n", av_err2str(err));
	    goto end;
	}

	AVFrame *enc_input = mapped_frame;

	if (ctx->is_software_encoder) {
		AVFrame *soft_frame = av_frame_alloc();
		av_hwframe_transfer_data(soft_frame, mapped_frame, 0);
		av_frame_free(&mapped_frame);
		enc_input = soft_frame;
	}

	/* Nanoseconds */
	enc_input->pts = (((uint64_t)tv_sec_hi) << 32) | tv_sec_lo;
	enc_input->pts *= 1000000000;
	enc_input->pts += tv_nsec;

	if (!ctx->start_pts) {
		ctx->start_pts = enc_input->pts;
	}

	enc_input->pts -= ctx->start_pts;

	enc_input->pts = av_rescale_q(enc_input->pts, (AVRational){ 1, 1000000000 },
			ctx->avctx->time_base);

	do {
		err = avcodec_send_frame(ctx->avctx, enc_input);

        av_frame_free(&enc_input);

		if (err) {
			av_log(ctx, AV_LOG_ERROR, "Error encoding: %s!\n", av_err2str(err));
			goto end;
		}

		while (1) {
			AVPacket pkt;
			av_init_packet(&pkt);

			int ret = avcodec_receive_packet(ctx->avctx, &pkt);
			if (ret == AVERROR(EAGAIN)) {
				break;
			} else if (ret == AVERROR_EOF) {
				av_log(ctx, AV_LOG_INFO, "Encoder flushed!\n");
				ctx->quit = 2;
				goto end;
			} else if (ret) {
				av_log(ctx, AV_LOG_ERROR, "Error encoding: %s!\n",
						av_err2str(ret));
				err = ret;
				goto end;
			}

			pkt.stream_index = 0;
			err = av_interleaved_write_frame(ctx->avf, &pkt);

			av_packet_unref(&pkt);

			if (err) {
				av_log(ctx, AV_LOG_ERROR, "Writing packet fail: %s!\n",
						av_err2str(err));
				goto end;
			}
		};
	} while (ctx->quit);

	av_log(NULL, AV_LOG_INFO, "Encoded frame %i!\n", ctx->avctx->frame_number);

	register_cb(ctx);

end:
	ctx->err = err;
	av_frame_free(&ctx->current_frame);
}

static void frame_cancel(void *data, struct zwlr_export_dmabuf_frame_v1 *frame,
		uint32_t reason) {
	struct capture_context *ctx = data;
	av_log(ctx, AV_LOG_WARNING, "Frame cancelled!\n");
	av_frame_free(&ctx->current_frame);
	if (reason != ZWLR_EXPORT_DMABUF_FRAME_V1_CANCEL_REASON_PERNAMENT)
		register_cb(ctx);
}

static const struct zwlr_export_dmabuf_frame_v1_listener frame_listener = {
	frame_start,
	frame_object,
	frame_plane,
	frame_ready,
	frame_cancel,
};

static void register_cb(struct capture_context *ctx)
{
	ctx->frame_callback =
		zwlr_export_dmabuf_manager_v1_capture_output(ctx->export_manager, 0,
				ctx->target_output);

	zwlr_export_dmabuf_frame_v1_add_listener(ctx->frame_callback,
			&frame_listener, ctx);
}

static int init_lavu_hwcontext(struct capture_context *ctx) {

	/* DRM hwcontext */
	ctx->drm_device_ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_DRM);
	if (!ctx->drm_device_ref)
		return AVERROR(ENOMEM);

	AVHWDeviceContext *ref_data = (AVHWDeviceContext*)ctx->drm_device_ref->data;
	AVDRMDeviceContext *hwctx = ref_data->hwctx;

	/* We don't need a device (we don't even know it and can't open it) */
	hwctx->fd = -1;

	av_hwdevice_ctx_init(ctx->drm_device_ref);

	/* Mapped hwcontext */
	int err = av_hwdevice_ctx_create(&ctx->mapped_device_ref,
			ctx->hw_device_type, ctx->hardware_device, NULL, 0);
	if (err < 0) {
		av_log(ctx, AV_LOG_ERROR, "Failed to create a hardware device: %s\n",
				av_err2str(err));
		return err;
	}

	return 0;
}

static int set_hwframe_ctx(struct capture_context *ctx,
		AVBufferRef *hw_device_ctx)
{
    AVHWFramesContext *frames_ctx = NULL;
    int err = 0;

    if (!(ctx->mapped_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx))) {
        return AVERROR(ENOMEM);
    }

	AVHWFramesConstraints *cst =
			av_hwdevice_get_hwframe_constraints(ctx->mapped_device_ref, NULL);
	if (!cst) {
		av_log(ctx, AV_LOG_ERROR, "Failed to get hw device constraints!\n");
		av_buffer_unref(&ctx->mapped_frames_ref);
		return AVERROR(ENOMEM);
	}

	frames_ctx = (AVHWFramesContext *)(ctx->mapped_frames_ref->data);
	frames_ctx->format    = cst->valid_hw_formats[0];
	frames_ctx->sw_format = ctx->avctx->pix_fmt;
	frames_ctx->width     = ctx->avctx->width;
	frames_ctx->height    = ctx->avctx->height;
	frames_ctx->initial_pool_size = 16;

	av_hwframe_constraints_free(&cst);

	if ((err = av_hwframe_ctx_init(ctx->mapped_frames_ref)) < 0) {
		av_log(ctx, AV_LOG_ERROR, "Failed to initialize hw frame context: %s!\n",
				av_err2str(err));
		av_buffer_unref(&ctx->mapped_frames_ref);
		return err;
	}

	if (!ctx->is_software_encoder) {
		ctx->avctx->pix_fmt = frames_ctx->format;
		ctx->avctx->hw_frames_ctx = av_buffer_ref(ctx->mapped_frames_ref);
		if (!ctx->avctx->hw_frames_ctx) {
			av_buffer_unref(&ctx->mapped_frames_ref);
			err = AVERROR(ENOMEM);
		}
	}

    return err;
}

static int init_encoding(struct capture_context *ctx) {
	int err;

	/* lavf init */
	err = avformat_alloc_output_context2(&ctx->avf, NULL,
			NULL, ctx->out_filename);
    if (err) {
        av_log(ctx, AV_LOG_ERROR, "Unable to init lavf context!\n");
        return err;
    }

    AVStream *st = avformat_new_stream(ctx->avf, NULL);
    if (!st) {
        av_log(ctx, AV_LOG_ERROR, "Unable to alloc stream!\n");
        return 1;
    }

	/* Find encoder */
    AVCodec *out_codec = avcodec_find_encoder_by_name(ctx->encoder_name);
    if (!out_codec) {
        av_log(ctx, AV_LOG_ERROR, "Codec not found (not compiled in lavc?)!\n");
        return AVERROR(EINVAL);
    }
    ctx->avf->oformat->video_codec = out_codec->id;
    ctx->is_software_encoder = !(out_codec->capabilities & AV_CODEC_CAP_HARDWARE);

	ctx->avctx = avcodec_alloc_context3(out_codec);
    if (!ctx->avctx)
        return 1;

    ctx->avctx->opaque            = ctx;
    ctx->avctx->bit_rate          = (int)ctx->out_bitrate*1000000.0f;
    ctx->avctx->pix_fmt           = ctx->software_format;
    ctx->avctx->time_base         = (AVRational){ 1, 1000 };
    ctx->avctx->compression_level = 7;
    ctx->avctx->width             = find_output(ctx, ctx->target_output, 0)->width;
    ctx->avctx->height            = find_output(ctx, ctx->target_output, 0)->height;

	if (ctx->avf->oformat->flags & AVFMT_GLOBALHEADER)
		ctx->avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	st->id             = 0;
    st->time_base      = ctx->avctx->time_base;
    st->avg_frame_rate = find_output(ctx, ctx->target_output, 0)->framerate;

    /* Init hw frames context */
    err = set_hwframe_ctx(ctx, ctx->mapped_device_ref);
    if (err)
	    return err;

	err = avcodec_open2(ctx->avctx, out_codec, &ctx->encoder_opts);
	if (err) {
        av_log(ctx, AV_LOG_ERROR, "Cannot open encoder: %s!\n",
        		av_err2str(err));
        return err;
    }

	if (avcodec_parameters_from_context(st->codecpar, ctx->avctx) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Couldn't copy codec params: %s!\n",
        		av_err2str(err));
        return err;
    }

    /* Debug print */
    av_dump_format(ctx->avf, 0, ctx->out_filename, 1);

    /* Open for writing */
    err = avio_open(&ctx->avf->pb, ctx->out_filename, AVIO_FLAG_WRITE);
    if (err) {
        av_log(ctx, AV_LOG_ERROR, "Couldn't open %s: %s!\n", ctx->out_filename,
        		av_err2str(err));
        return err;
    }

	err = avformat_write_header(ctx->avf, NULL);
    if (err) {
        av_log(ctx, AV_LOG_ERROR, "Couldn't write header: %s!\n", av_err2str(err));
        return err;
    }

	return err;
}

struct capture_context *q_ctx = NULL;

void on_quit_signal(int signo) {
	printf("\r");
	q_ctx->quit = 1;
}

static int main_loop(struct capture_context *ctx) {
	int err;

    q_ctx = ctx;

    if (signal(SIGINT, on_quit_signal) == SIG_ERR) {
		av_log(ctx, AV_LOG_ERROR, "Unable to install signal handler!\n");
        return AVERROR(EINVAL);
    }

	err = init_lavu_hwcontext(ctx);
	if (err)
		return err;

	err = init_encoding(ctx);
	if (err)
		return err;

	/* Start the frame callback */
	register_cb(ctx);

	while (!ctx->err && ctx->quit < 2) {
	    while (wl_display_prepare_read(ctx->display) != 0) {
			wl_display_dispatch_pending(ctx->display);
		}

        wl_display_flush(ctx->display);

		struct pollfd fds[1] = {
			{ .fd = wl_display_get_fd(ctx->display), .events = POLLIN },
		};

		poll(fds, 1, -1);

		if (!(fds[0].revents & POLLIN)) {
			wl_display_cancel_read(ctx->display);
		}

		if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			av_log(ctx, AV_LOG_ERROR, "Error occurred on the display fd!\n");
			break;
		}

		if (fds[0].revents & POLLIN) {
		    if (wl_display_read_events(ctx->display) < 0) {
				av_log(ctx, AV_LOG_ERROR, "Failed to read Wayland events!\n");
				break;
			}
			wl_display_dispatch_pending(ctx->display);
		}
	}

	err = av_write_trailer(ctx->avf);
    if (err) {
        av_log(ctx, AV_LOG_ERROR, "Error writing trailer: %s!\n",
        		av_err2str(err));
        return err;
    }

    av_log(ctx, AV_LOG_INFO, "Wrote trailer!\n");

	return ctx->err;
}

static int init(struct capture_context *ctx) {
	ctx->display = wl_display_connect(NULL);
	if (!ctx->display) {
		av_log(ctx, AV_LOG_ERROR, "Failed to connect to display!\n");
		return AVERROR(EINVAL);
	}

	wl_list_init(&ctx->output_list);

	ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);

	wl_display_roundtrip(ctx->display);
	wl_display_dispatch(ctx->display);

	if (!ctx->export_manager) {
		av_log(ctx, AV_LOG_ERROR, "Compositor doesn't support %s!\n",
				zwlr_export_dmabuf_manager_v1_interface.name);
		return -1;
	}

	return 0;
}

static void print_capturable_surfaces(struct capture_context *ctx) {

	struct wayland_output *o, *tmp_o;
	wl_list_for_each_reverse_safe(o, tmp_o, &ctx->output_list, link) {
		ctx->target_output = o->output; /* Default is first, whatever */
		av_log(ctx, AV_LOG_INFO, "Capturable output: %s Model: %s:\n",
				o->make, o->model);
	}

	av_log(ctx, AV_LOG_INFO, "Capturing from output: %s!\n",
			find_output(ctx, ctx->target_output, 0)->model);
}

static void uninit(struct capture_context *ctx);

int main(int argc, char *argv[]) {
	int err;
	struct capture_context ctx = { 0 };
	ctx.class = &((AVClass) {
		.class_name = "dmabuf-capture",
		.item_name  = av_default_item_name,
		.version    = LIBAVUTIL_VERSION_INT,
	});

	err = init(&ctx);
	if (err)
		goto end;

	print_capturable_surfaces(&ctx);

	ctx.hw_device_type = av_hwdevice_find_type_by_name("vaapi");
	ctx.hardware_device = "/dev/dri/renderD128";

	ctx.encoder_name = "libx264";
	ctx.software_format = av_get_pix_fmt("nv12");
	av_dict_set(&ctx.encoder_opts, "preset", "veryfast", 0);

	ctx.out_filename = "dmabuf_recording_01.mkv";
	ctx.out_bitrate = 29.2f; /* Mbps */

	err = main_loop(&ctx);
	if (err)
		goto end;

end:
	uninit(&ctx);
	return err;
}

static void uninit(struct capture_context *ctx) {
	struct wayland_output *output, *tmp_o;
	wl_list_for_each_safe(output, tmp_o, &ctx->output_list, link)
		remove_output(output);

	if (ctx->export_manager)
		zwlr_export_dmabuf_manager_v1_destroy(ctx->export_manager);

	av_buffer_unref(&ctx->drm_frames_ref);
    av_buffer_unref(&ctx->drm_device_ref);
    av_buffer_unref(&ctx->mapped_frames_ref);
    av_buffer_unref(&ctx->mapped_device_ref);

	av_dict_free(&ctx->encoder_opts);

	avcodec_close(ctx->avctx);
	if (ctx->avf) {
		avio_closep(&ctx->avf->pb);
	}
	avformat_free_context(ctx->avf);
}
