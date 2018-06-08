#include <stdlib.h>
#include <limits.h>
#include <wlr/util/log.h>
#include "rootston/output.h"
#include "rootston/layout.h"

static struct roots_layout_rule *get_rule(struct roots_layout *layout, char *name) {
	struct roots_layout_rule *rule;
	wl_list_for_each(rule, &layout->rules, link) {
		if (strcmp(name, rule->output->wlr_output->name) == 0) {
			return rule;
		}
	}

	return NULL;
}

struct roots_layout* roots_layout_create(struct roots_layout_config *config) {
	struct roots_layout *layout = calloc(1, sizeof(struct roots_layout));

	if (!layout) {
		return NULL;
	}

	wl_list_init(&layout->rules);
	layout->wlr_layout = wlr_output_layout_create();
	layout->current_config = config;

	return layout;
}

void roots_layout_destroy(struct roots_layout *layout) {
	if (layout) {
		struct roots_layout_rule *rule, *tmp;
		wl_list_for_each_safe(rule, tmp, &layout->rules, link) {
			wl_list_remove(&rule->link);
			free(rule);
		}

		free(layout);
	}
}

static void apply_rule(struct roots_layout *layout,
		struct roots_layout_rule *rule) {

	rule->configured = false;

	if (rule->config) {
		if (rule->config->configuration !=
				WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_FIXED) {
			struct roots_layout_rule *ref =
				get_rule(layout, rule->config->reference_output);
			if (ref) {
				wlr_output_layout_add_relative(layout->wlr_layout,
						rule->output->wlr_output, ref->output->wlr_output,
						rule->config->configuration);
				rule->configured = true;
			} else {
				wlr_output_layout_add(layout->wlr_layout,
						rule->output->wlr_output, 0, 0);
			}
		} else {
			wlr_output_layout_add(layout->wlr_layout, rule->output->wlr_output,
					rule->config->x, rule->config->y);
			rule->configured = true;
		}
	} else {
		wlr_output_layout_add(layout->wlr_layout,
				rule->output->wlr_output, 0, 0);
	}

	struct roots_layout_rule *ref_rule;
	wl_list_for_each(ref_rule, &layout->rules, link) {
		if (!ref_rule->configured && ref_rule->config) {
			if (strcmp(rule->output->wlr_output->name,
						ref_rule->config->reference_output) == 0) {
				wlr_output_layout_add_relative(layout->wlr_layout,
						ref_rule->output->wlr_output, rule->output->wlr_output,
						ref_rule->config->configuration);
				ref_rule->configured = true;
			}
		}
	}
}

void roots_layout_add_output(struct roots_layout *layout,
		struct roots_output *output) {

	struct roots_layout_rule *new_rule;

	wl_list_for_each(new_rule, &layout->rules, link) {
		if (new_rule->output == output) {
			return;
		}
	}

	new_rule = calloc(1, sizeof(struct roots_layout_rule));

	if (!new_rule) {
		wlr_log(L_ERROR, "Could not allocate layout rule");
		return;
	}

	new_rule->output = output;

	struct roots_layout_rule_config *c_rule;
	wl_list_for_each(c_rule, &layout->current_config->rules, link) {
		if (strcmp(output->wlr_output->name, c_rule->output_name) == 0) {
			new_rule->config = c_rule;
			break;
		}
	}

	wl_list_insert(&layout->rules, &new_rule->link);
	apply_rule(layout, new_rule);
}

void roots_layout_remove_output(struct roots_layout *layout,
		struct roots_output *output) {
	struct roots_layout_rule *rule, *tmp;

	wl_list_for_each_safe(rule, tmp, &layout->rules, link) {
		if (rule->output == output) {
			wl_list_remove(&rule->link);
			free(rule);
		} else if (rule->config && rule->config->reference_output &&
				strcmp(output->wlr_output->name,
					rule->config->reference_output) == 0) {
			rule->configured = false;
		}
	}
}

/*
 * roots_layout_reflow makes sure that outputs that could not be properly
 * configured, do not cause gaps or overlaps. First the extents of the
 * outputs that are fully configured are calculated. These include outputs
 * that have no active reference and are not fixed, and their children.
 * The remaining unconfigured outputs are placed to the right of the fully
 * configured outputs, taking into account that the unconfigred outputs
 * can have children.
 */
void roots_layout_reflow(struct roots_layout *layout) {
	int max_x = INT_MIN;
	int max_x_y = 0;
	// Configured is always initialized before use, since the first output
	// in the layout is always fixed (rootston specifc, since it does not
	// use auto outputs)
	bool configured;

	// XXX: requires specific ordering of the outputs in layout->outputs,
	// specifically, output must follow their reference immediately.
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->wlr_layout->outputs, link) {
		if (l_output->configuration ==
				WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_FIXED) {
			struct roots_layout_rule *rule;
			wl_list_for_each(rule, &layout->rules, link) {
				if (rule->output->wlr_output == l_output->output) {
					configured = rule->configured;
					break;
				}
			}
		}

		if (configured) {
			struct wlr_box *box =
				wlr_output_layout_get_box(layout->wlr_layout, l_output->output);
			if (max_x < box->x + box->width) {
				max_x = box->x + box->width;
				max_x_y = box->y;
			}
		}
	}

	if (max_x == INT_MIN) { // In case there are no configured layouts
		max_x = 0;
	}

	struct wlr_output *unconfigured_output = NULL;
	int auto_min_x = INT_MAX;
	int auto_min_x_y = 0;
	int auto_max_x = INT_MIN;
	int auto_max_x_y = 0;

	wl_list_for_each(l_output, &layout->wlr_layout->outputs, link) {
		if (l_output->configuration ==
				WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_FIXED) {
			if (unconfigured_output) {
				struct wlr_box *box =
					wlr_output_layout_get_box(layout->wlr_layout,
							unconfigured_output);
				wlr_output_layout_add(layout->wlr_layout, unconfigured_output,
						box->x - auto_min_x + max_x,
						box->y - auto_min_x_y + max_x_y);
				max_x += auto_max_x - auto_min_x;
				max_x_y += auto_max_x_y - auto_min_x_y;
				auto_min_x = INT_MAX;
				auto_min_x_y = 0;
				auto_max_x = INT_MIN;
				auto_max_x_y = 0;
				unconfigured_output = NULL;
			}
			struct roots_layout_rule *rule;
			wl_list_for_each(rule, &layout->rules, link) {
				if (rule->output->wlr_output == l_output->output) {
					configured = rule->configured;
					if (!configured) {
						unconfigured_output = l_output->output;
					}
					break;
				}
			}
		}

		if (!configured) {
			struct wlr_box *box =
				wlr_output_layout_get_box(layout->wlr_layout, l_output->output);
			if (auto_max_x < box->x + box->width) {
				auto_max_x = box->x + box->width;
				auto_max_x_y = box->y;
			}

			if (auto_min_x > box->x) {
				auto_min_x = box->x;
				auto_min_x_y = box->y;
			}
		}
	}

	if (unconfigured_output) {
		struct wlr_box *box =
			wlr_output_layout_get_box(layout->wlr_layout,
					unconfigured_output);
		wlr_output_layout_add(layout->wlr_layout, unconfigured_output,
				box->x - auto_min_x + max_x,
				box->y - auto_min_x_y + max_x_y);
		max_x += auto_max_x - auto_min_x;
		max_x_y += auto_max_x_y - auto_min_x_y;
	}

	struct roots_layout_rule *rule;
	wl_list_for_each(rule, &layout->rules, link) {
		output_damage_whole(rule->output);
	}
}
