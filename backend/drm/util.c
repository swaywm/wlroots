#include <assert.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <drm.h>
#include <gbm.h>
#include <stdio.h>
#include <string.h>
#include <wlr/util/log.h>
#include "backend/drm/util.h"

int32_t calculate_refresh_rate(const drmModeModeInfo *mode) {
	int32_t refresh = (mode->clock * 1000000LL / mode->htotal +
		mode->vtotal / 2) / mode->vtotal;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		refresh *= 2;
	}

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		refresh /= 2;
	}

	if (mode->vscan > 1) {
		refresh /= mode->vscan;
	}

	return refresh;
}

// Constructed from http://edid.tv/manufacturer
static const char *get_manufacturer(uint16_t id) {
#define ID(a, b, c) ((a & 0x1f) << 10) | ((b & 0x1f) << 5) | (c & 0x1f)
	switch (id) {
	case ID('A', 'A', 'A'): return "Avolites Ltd";
	case ID('A', 'C', 'I'): return "Ancor Communications Inc";
	case ID('A', 'C', 'R'): return "Acer Technologies";
	case ID('A', 'D', 'A'): return "Addi-Data GmbH";
	case ID('A', 'P', 'P'): return "Apple Computer Inc";
	case ID('A', 'S', 'K'): return "Ask A/S";
	case ID('A', 'V', 'T'): return "Avtek (Electronics) Pty Ltd";
	case ID('B', 'N', 'O'): return "Bang & Olufsen";
	case ID('B', 'N', 'Q'): return "BenQ Corporation";
	case ID('C', 'M', 'N'): return "Chimei Innolux Corporation";
	case ID('C', 'M', 'O'): return "Chi Mei Optoelectronics corp.";
	case ID('C', 'R', 'O'): return "Extraordinary Technologies PTY Limited";
	case ID('D', 'E', 'L'): return "Dell Inc.";
	case ID('D', 'G', 'C'): return "Data General Corporation";
	case ID('D', 'O', 'N'): return "DENON, Ltd.";
	case ID('E', 'N', 'C'): return "Eizo Nanao Corporation";
	case ID('E', 'P', 'H'): return "Epiphan Systems Inc.";
	case ID('E', 'X', 'P'): return "Data Export Corporation";
	case ID('F', 'N', 'I'): return "Funai Electric Co., Ltd.";
	case ID('F', 'U', 'S'): return "Fujitsu Siemens Computers GmbH";
	case ID('G', 'S', 'M'): return "Goldstar Company Ltd";
	case ID('H', 'I', 'Q'): return "Kaohsiung Opto Electronics Americas, Inc.";
	case ID('H', 'S', 'D'): return "HannStar Display Corp";
	case ID('H', 'T', 'C'): return "Hitachi Ltd";
	case ID('H', 'W', 'P'): return "Hewlett Packard";
	case ID('I', 'N', 'T'): return "Interphase Corporation";
	case ID('I', 'N', 'X'): return "Communications Supply Corporation (A division of WESCO)";
	case ID('I', 'T', 'E'): return "Integrated Tech Express Inc";
	case ID('I', 'V', 'M'): return "Iiyama North America";
	case ID('L', 'E', 'N'): return "Lenovo Group Limited";
	case ID('M', 'A', 'X'): return "Rogen Tech Distribution Inc";
	case ID('M', 'E', 'G'): return "Abeam Tech Ltd";
	case ID('M', 'E', 'I'): return "Panasonic Industry Company";
	case ID('M', 'T', 'C'): return "Mars-Tech Corporation";
	case ID('M', 'T', 'X'): return "Matrox";
	case ID('N', 'E', 'C'): return "NEC Corporation";
	case ID('N', 'E', 'X'): return "Nexgen Mediatech Inc.";
	case ID('O', 'N', 'K'): return "ONKYO Corporation";
	case ID('O', 'R', 'N'): return "ORION ELECTRIC CO., LTD.";
	case ID('O', 'T', 'M'): return "Optoma Corporation";
	case ID('O', 'V', 'R'): return "Oculus VR, Inc.";
	case ID('P', 'H', 'L'): return "Philips Consumer Electronics Company";
	case ID('P', 'I', 'O'): return "Pioneer Electronic Corporation";
	case ID('P', 'N', 'R'): return "Planar Systems, Inc.";
	case ID('Q', 'D', 'S'): return "Quanta Display Inc.";
	case ID('R', 'A', 'T'): return "Rent-A-Tech";
	case ID('R', 'E', 'N'): return "Renesas Technology Corp.";
	case ID('S', 'A', 'M'): return "Samsung Electric Company";
	case ID('S', 'A', 'N'): return "Sanyo Electric Co., Ltd.";
	case ID('S', 'E', 'C'): return "Seiko Epson Corporation";
	case ID('S', 'H', 'P'): return "Sharp Corporation";
	case ID('S', 'I', 'I'): return "Silicon Image, Inc.";
	case ID('S', 'N', 'Y'): return "Sony";
	case ID('S', 'T', 'D'): return "STD Computer Inc";
	case ID('S', 'V', 'S'): return "SVSI";
	case ID('S', 'Y', 'N'): return "Synaptics Inc";
	case ID('T', 'C', 'L'): return "Technical Concepts Ltd";
	case ID('T', 'O', 'P'): return "Orion Communications Co., Ltd.";
	case ID('T', 'S', 'B'): return "Toshiba America Info Systems Inc";
	case ID('T', 'S', 'T'): return "Transtream Inc";
	case ID('U', 'N', 'K'): return "Unknown";
	case ID('V', 'E', 'S'): return "Vestel Elektronik Sanayi ve Ticaret A. S.";
	case ID('V', 'I', 'T'): return "Visitech AS";
	case ID('V', 'I', 'Z'): return "VIZIO, Inc";
	case ID('V', 'S', 'C'): return "ViewSonic Corporation";
	case ID('Y', 'M', 'H'): return "Yamaha Corporation";
	default:                return "Unknown";
	}
#undef ID
}

/* See https://en.wikipedia.org/wiki/Extended_Display_Identification_Data for layout of EDID data.
 * We don't parse the EDID properly. We just expect to receive valid data.
 */
void parse_edid(struct wlr_output *restrict output, size_t len, const uint8_t *data) {
	if (!data || len < 128) {
		snprintf(output->make, sizeof(output->make), "<Unknown>");
		snprintf(output->model, sizeof(output->model), "<Unknown>");
		return;
	}

	uint16_t id = (data[8] << 8) | data[9];
	snprintf(output->make, sizeof(output->make), "%s", get_manufacturer(id));

	uint16_t model = data[10] | (data[11] << 8);
	snprintf(output->model, sizeof(output->model), "0x%04X", model);

	uint32_t serial = data[12] | (data[13] << 8) | (data[14] << 8) | (data[15] << 8);
	snprintf(output->serial, sizeof(output->serial), "0x%08X", serial);

	for (size_t i = 72; i <= 108; i += 18) {
		uint16_t flag = (data[i] << 8) | data[i + 1];
		if (flag == 0 && data[i + 3] == 0xFC) {
			sprintf(output->model, "%.13s", &data[i + 5]);

			// Monitor names are terminated by newline if they're too short
			char *nl = strchr(output->model, '\n');
			if (nl) {
				*nl = '\0';
			}
		} else if (flag == 0 && data[i + 3] == 0xFF) {
			sprintf(output->serial, "%.13s", &data[i + 5]);

			// Monitor serial numbers are terminated by newline if they're too
			// short
			char *nl = strchr(output->serial, '\n');
			if (nl) {
				*nl = '\0';
			}
		}
	}
}

void parse_tile(struct wlr_output *restrict output, size_t len, const uint8_t *data) {
	if (len > 0) {
		int ret;
		ret = sscanf((char*)data, "%d:%d:%d:%d:%d:%d:%d:%d",
			&output->tile_info.group_id,
			&output->tile_info.tile_is_single_monitor,
			&output->tile_info.num_h_tile,
			&output->tile_info.num_v_tile,
			&output->tile_info.tile_h_loc,
			&output->tile_info.tile_v_loc,
			&output->tile_info.tile_h_size,
			&output->tile_info.tile_v_size);
		if(ret != 8)
			wlr_log(WLR_ERROR, "Unable to understand tile information for "
				"output %s", output->name);
		else
			wlr_log(WLR_INFO, "Output %s TILE information: "
				"group ID %d, single monitor %d, total %d horizontal tiles, "
				"total %d vertical tiles, horizontal tile %d, vertical tile "
				"%d, width %d, height %d",
				output->name,
				output->tile_info.group_id,
				output->tile_info.tile_is_single_monitor,
				output->tile_info.num_h_tile,
				output->tile_info.num_v_tile,
				output->tile_info.tile_h_loc,
				output->tile_info.tile_v_loc,
				output->tile_info.tile_h_size,
				output->tile_info.tile_v_size);
	}
	else
		wlr_log(WLR_DEBUG, "No tile information available for output %s",
			output->name);
}

const char *conn_get_name(uint32_t type_id) {
	switch (type_id) {
	case DRM_MODE_CONNECTOR_Unknown:     return "Unknown";
	case DRM_MODE_CONNECTOR_VGA:         return "VGA";
	case DRM_MODE_CONNECTOR_DVII:        return "DVI-I";
	case DRM_MODE_CONNECTOR_DVID:        return "DVI-D";
	case DRM_MODE_CONNECTOR_DVIA:        return "DVI-A";
	case DRM_MODE_CONNECTOR_Composite:   return "Composite";
	case DRM_MODE_CONNECTOR_SVIDEO:      return "SVIDEO";
	case DRM_MODE_CONNECTOR_LVDS:        return "LVDS";
	case DRM_MODE_CONNECTOR_Component:   return "Component";
	case DRM_MODE_CONNECTOR_9PinDIN:     return "DIN";
	case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
	case DRM_MODE_CONNECTOR_HDMIA:       return "HDMI-A";
	case DRM_MODE_CONNECTOR_HDMIB:       return "HDMI-B";
	case DRM_MODE_CONNECTOR_TV:          return "TV";
	case DRM_MODE_CONNECTOR_eDP:         return "eDP";
	case DRM_MODE_CONNECTOR_VIRTUAL:     return "Virtual";
	case DRM_MODE_CONNECTOR_DSI:         return "DSI";
	case DRM_MODE_CONNECTOR_DPI:         return "DPI";
	case DRM_MODE_CONNECTOR_WRITEBACK:   return "Writeback";
#ifdef DRM_MODE_CONNECTOR_SPI
	case DRM_MODE_CONNECTOR_SPI:         return "SPI";
#endif
#ifdef DRM_MODE_CONNECTOR_USB
	case DRM_MODE_CONNECTOR_USB:         return "USB";
#endif
	default:                             return "Unknown";
	}
}

uint32_t get_fb_for_bo(struct gbm_bo *bo, bool with_modifiers) {
	struct gbm_device *gbm = gbm_bo_get_device(bo);

	int fd = gbm_device_get_fd(gbm);
	uint32_t width = gbm_bo_get_width(bo);
	uint32_t height = gbm_bo_get_height(bo);
	uint32_t format = gbm_bo_get_format(bo);

	uint32_t handles[4] = {0};
	uint32_t strides[4] = {0};
	uint32_t offsets[4] = {0};
	uint64_t modifiers[4] = {0};
	for (int i = 0; i < gbm_bo_get_plane_count(bo); i++) {
		handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
		strides[i] = gbm_bo_get_stride_for_plane(bo, i);
		offsets[i] = gbm_bo_get_offset(bo, i);
		// KMS requires all BO planes to have the same modifier
		modifiers[i] = gbm_bo_get_modifier(bo);
	}

	uint32_t id = 0;
	if (with_modifiers && gbm_bo_get_modifier(bo) != DRM_FORMAT_MOD_INVALID) {
		if (drmModeAddFB2WithModifiers(fd, width, height, format, handles,
				strides, offsets, modifiers, &id, DRM_MODE_FB_MODIFIERS)) {
			wlr_log_errno(WLR_ERROR, "Unable to add DRM framebuffer");
		}
	} else {
		int ret = drmModeAddFB2(fd, width, height, format, handles, strides,
			offsets, &id, 0);
		if (ret != 0 && gbm_bo_get_format(bo) == GBM_FORMAT_ARGB8888 &&
				gbm_bo_get_plane_count(bo) == 1) {
			// Some big-endian machines don't support drmModeAddFB2. Try a
			// last-resort fallback for ARGB8888 buffers, like Xorg's
			// modesetting driver does.
			wlr_log(WLR_DEBUG, "drmModeAddFB2 failed (%s), falling back to "
				"legacy drmModeAddFB", strerror(-ret));

			uint32_t depth = 32;
			uint32_t bpp = gbm_bo_get_bpp(bo);
			ret = drmModeAddFB(fd, width, height, depth, bpp, strides[0],
				handles[0], &id);
		}
		if (ret != 0) {
			wlr_log(WLR_ERROR, "Unable to add DRM framebuffer: %s", strerror(-ret));
		}
	}

	return id;
}

static bool is_taken(size_t n, const uint32_t arr[static n], uint32_t key) {
	for (size_t i = 0; i < n; ++i) {
		if (arr[i] == key) {
			return true;
		}
	}
	return false;
}

/*
 * Store all of the non-recursive state in a struct, so we aren't literally
 * passing 12 arguments to a function.
 */
struct match_state {
	const size_t num_objs;
	const uint32_t *restrict objs;
	const size_t num_res;
	size_t score;
	size_t replaced;
	uint32_t *restrict res;
	uint32_t *restrict best;
	const uint32_t *restrict orig;
	bool exit_early;
};

/*
 * skips: The number of SKIP elements encountered so far.
 * score: The number of resources we've matched so far.
 * replaced: The number of changes from the original solution.
 * i: The index of the current element.
 *
 * This tries to match a solution as close to st->orig as it can.
 *
 * Returns whether we've set a new best element with this solution.
 */
static bool match_obj_(struct match_state *st, size_t skips, size_t score, size_t replaced, size_t i) {
	// Finished
	if (i >= st->num_res) {
		if (score > st->score ||
				(score == st->score && replaced < st->replaced)) {
			st->score = score;
			st->replaced = replaced;
			memcpy(st->best, st->res, sizeof(st->best[0]) * st->num_res);

			st->exit_early = (st->score == st->num_res - skips
					|| st->score == st->num_objs)
					&& st->replaced == 0;

			return true;
		} else {
			return false;
		}
	}

	if (st->orig[i] == SKIP) {
		st->res[i] = SKIP;
		return match_obj_(st, skips + 1, score, replaced, i + 1);
	}

	bool has_best = false;

	/*
	 * Attempt to use the current solution first, to try and avoid
	 * recalculating everything
	 */
	if (st->orig[i] != UNMATCHED && !is_taken(i, st->res, st->orig[i])) {
		st->res[i] = st->orig[i];
		size_t obj_score = st->objs[st->res[i]] != 0 ? 1 : 0;
		if (match_obj_(st, skips, score + obj_score, replaced, i + 1)) {
			has_best = true;
		}
	}
	if (st->orig[i] == UNMATCHED) {
		st->res[i] = UNMATCHED;
		if (match_obj_(st, skips, score, replaced, i + 1)) {
			has_best = true;
		}
	}
	if (st->exit_early) {
		return true;
	}

	if (st->orig[i] != UNMATCHED) {
		++replaced;
	}

	for (size_t candidate = 0; candidate < st->num_objs; ++candidate) {
		// We tried this earlier
		if (candidate == st->orig[i]) {
			continue;
		}

		// Not compatible
		if (!(st->objs[candidate] & (1 << i))) {
			continue;
		}

		// Already taken
		if (is_taken(i, st->res, candidate)) {
			continue;
		}

		st->res[i] = candidate;
		size_t obj_score = st->objs[candidate] != 0 ? 1 : 0;
		if (match_obj_(st, skips, score + obj_score, replaced, i + 1)) {
			has_best = true;
		}

		if (st->exit_early) {
			return true;
		}
	}

	if (has_best) {
		return true;
	}

	// Maybe this resource can't be matched
	st->res[i] = UNMATCHED;
	return match_obj_(st, skips, score, replaced, i + 1);
}

size_t match_obj(size_t num_objs, const uint32_t objs[static restrict num_objs],
		size_t num_res, const uint32_t res[static restrict num_res],
		uint32_t out[static restrict num_res]) {
	uint32_t solution[num_res];
	for (size_t i = 0; i < num_res; ++i) {
		solution[i] = UNMATCHED;
	}

	struct match_state st = {
		.num_objs = num_objs,
		.num_res = num_res,
		.score = 0,
		.replaced = SIZE_MAX,
		.objs = objs,
		.res = solution,
		.best = out,
		.orig = res,
		.exit_early = false,
	};

	match_obj_(&st, 0, 0, 0, 0);
	return st.score;
}
