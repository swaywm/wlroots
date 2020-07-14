#ifndef _WLR_COLOR_H
#define _WLR_COLOR_H

struct wlr_color_config {
    char *icc_profile_path; // not null
};

/**
 * Create a color config.
 *
 * icc_profile_path should not be NULL.
 * It will be copied into the config.
 */
struct wlr_color_config *wlr_color_config_load(const char *icc_profile_path);

struct wlr_color_config *wlr_color_config_copy(struct wlr_color_config *value);
void wlr_color_config_free(struct wlr_color_config *config);

#endif
