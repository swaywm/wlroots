// OpenGL Color conversion

#define _XOPEN_SOURCE 500 // for strdup

#include <GLES3/gl32.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>

#include <wlr/types/wlr_list.h>
#include <wlr/render/color.h>

#include <lcms2.h>

#include "render/gles2.h"

// GL3+ or GL_EXT_texture_norm16 on ES 3.2
#define GL_RGBA16 0x805B

struct lut {
	GLuint tex_id;
	struct wlr_color_config *input, *output;
	cmsHTRANSFORM transform;
};

#define LUTS_MAX 100

static struct wlr_list luts;

static int color_compare(void *av, void *bv) {
	struct wlr_color_config *a = av, *b = bv;
	if(a == b)
		return 0;
	if(!a)
		return 1;
	if(!b)
		return -1;

	return strcmp(a->icc_profile_path, b->icc_profile_path);
}

struct wlr_color_config *wlr_color_config_copy(struct wlr_color_config *value) {
	if(! value)
		return NULL;

	struct wlr_color_config *copy = calloc(1, sizeof(*copy));
	copy->icc_profile_path = strdup(value->icc_profile_path);
	return copy;
}

static cmsHPROFILE color_load(struct wlr_color_config *color) {
	if(color) {
		cmsHPROFILE pro = cmsOpenProfileFromFile (color->icc_profile_path, "r");
		if (pro == NULL) {
			wlr_log(WLR_ERROR, "error loading icc profile %s", color->icc_profile_path);
		} else {
			return pro;
		}
	}
	return cmsCreate_sRGBProfile();

}

static const char *color_describe(struct wlr_color_config *color) {
	if(! color)
		return "sRGB";

	char *base = strrchr(color->icc_profile_path, '/');
	return base ? base + 1 : color->icc_profile_path;
}

struct wlr_color_config *wlr_color_config_load(const char *icc_profile_path) {
	assert(icc_profile_path);

	bool can_access = access(icc_profile_path, F_OK) != -1;
	if (!can_access) {
		wlr_log(WLR_ERROR, "Unable to access color profile '%s'", icc_profile_path);
		return NULL;
	}

	struct wlr_color_config *color = calloc(1, sizeof(*color));
	color->icc_profile_path = strdup(icc_profile_path);
	return color;
}

void wlr_color_config_free(struct wlr_color_config *color) {
	if(! color)
		return;
	free(color->icc_profile_path);
	free(color);
}

static struct lut *load_lut(struct wlr_color_config *input, struct wlr_color_config *output) {
	if(0 == color_compare(input, output))
		return NULL;

	struct lut *lut;

	// find an existing cached lut
	for (size_t i = 0; i < luts.length; i++) {
		lut = luts.items[i];
		if(color_compare(lut->input, input) || color_compare(lut->output, output))
			continue;

		// move to head to list
		if(i > 5) {
			wlr_list_del(&luts, i);
			wlr_list_insert(&luts, 0, lut);
		}
		return lut;
	}

	// clean an old LUT to avoid memory leak
	if(luts.length > LUTS_MAX) {
		struct lut *lut = wlr_list_pop(&luts);
		wlr_log(WLR_DEBUG, "destroy CLUT %s -> %s", color_describe(lut->input), color_describe(lut->output));
		wlr_color_config_free(lut->input);
		wlr_color_config_free(lut->output);
		cmsDeleteTransform(lut->transform);
		glDeleteTextures(1, &lut->tex_id);
		free(lut);
	}

	wlr_log(WLR_INFO, "create CLUT %s -> %s", color_describe(input), color_describe(output));
	cmsHPROFILE inp = color_load(input);
	cmsHPROFILE outp = color_load(output);

	// According to lcms docs, black point compensation does not apply to perceptual intent, so we omit the flag.
	cmsHTRANSFORM xform = cmsCreateTransform(
			inp, TYPE_RGB_16,
			outp, TYPE_RGB_16,
			INTENT_PERCEPTUAL,
			cmsFLAGS_HIGHRESPRECALC | cmsFLAGS_NOCACHE);

	cmsCloseProfile(inp);
	cmsCloseProfile(outp);

	if (xform == NULL) {
		wlr_log(WLR_ERROR, "failed to create color transform");
		return NULL;
	}

	// save the new LUT
	lut = malloc(sizeof(*lut));
	*lut = (struct lut){
		.input = wlr_color_config_copy(input),
			.output = wlr_color_config_copy(output),
			.transform = xform,
	};
	wlr_list_insert(&luts, 0, lut);
	return lut;
}

void color_convert(struct wlr_color_config *ic, struct wlr_color_config *oc, const float input[static 4], float output[static 4]) {
	struct lut *lut = load_lut(ic, oc);
	if(! lut) {
		memcpy(output, input, 4 * sizeof(float));
		return;
	}

	uint16_t out[3], in[3] = {
		input[0] * UINT16_MAX,
		input[1] * UINT16_MAX,
		input[2] * UINT16_MAX,
	};
	cmsDoTransform(lut->transform, in, out, 1);
	output[0] = (float)out[0] / UINT16_MAX;
	output[1] = (float)out[1] / UINT16_MAX;
	output[2] = (float)out[2] / UINT16_MAX;
	output[3] = input[3];
}

GLuint color_build_lut(struct wlr_color_config *input, struct wlr_color_config *output) {
	struct lut *lut = load_lut(input, output);
	if(! lut)
		return 0;

	if(lut->tex_id)
		return lut->tex_id;

	// 3D array: [B][G][R] = [R,G,B,A]
	// Notes:
	// - The first dimension is blue
	// - The alpha channel is unused but needed since GL_RGB16 does not work.
	uint16_t table[COLOR_LUT_SIZE][COLOR_LUT_SIZE][COLOR_LUT_SIZE][4];

	int n = COLOR_LUT_SIZE;
	int r, g, b;
	for (r = 0; r < n; r++) {
		for (g = 0; g < n; g++) {
			for (b = 0; b < n; b++) {
				uint16_t in[3];
				in[0] = floor((double) r / (n - 1) * UINT16_MAX + 0.5);
				in[1] = floor((double) g / (n - 1) * UINT16_MAX + 0.5);
				in[2] = floor((double) b / (n - 1) * UINT16_MAX + 0.5);

				cmsDoTransform(lut->transform, in, table[b][g][r], 1);
			}
		}
	}

	glGenTextures (1, &lut->tex_id);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_3D, lut->tex_id);

	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	// we must use linear interpolation for small LUTs
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16, n, n, n,
			0, GL_RGBA, GL_UNSIGNED_SHORT, table);

	// cleanup
	glBindTexture(GL_TEXTURE_3D, 0);
	glActiveTexture(GL_TEXTURE0);

	return lut->tex_id;
}

static void lcms_error_handler(cmsContext ctx, cmsUInt32Number code, const char *msg) {
	wlr_log(WLR_ERROR, "color management: [%i] %s", code, msg);
}

void color_engine_setup(void) {
	if(luts.capacity == 0)
		wlr_list_init(&luts);
	cmsSetLogErrorHandler(lcms_error_handler);
}

