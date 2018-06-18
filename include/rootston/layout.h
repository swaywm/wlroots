#ifndef ROOTSTON_LAYOUT_H
#define ROOTSTON_LAYOUT_H

#include <wlr/types/wlr_output_layout.h>
#include "rootston/config.h"
#include "rootston/output.h"

struct roots_layout_rule {
	struct roots_layout_rule_config *config;
	struct roots_output *output;
	struct wl_list link;
	bool configured;
};

struct roots_layout {
	struct wlr_output_layout *wlr_layout;
	struct roots_layout_config *current_config;
	struct wl_list rules;
};

struct roots_layout* roots_layout_create(struct roots_layout_config *config);
void roots_layout_destroy(struct roots_layout *layout);
void roots_layout_add_output(struct roots_layout *layout,
		struct roots_output *output);
void roots_layout_remove_output(struct roots_layout *layout,
		struct roots_output *output);
void roots_layout_reflow(struct roots_layout *layout);

#endif
