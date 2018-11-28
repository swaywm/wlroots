#include <stdlib.h>

#include <wlr/util/log.h>

#include "rootston/bindings.h"
#include "rootston/config.h"
#include "rootston/input.h"
#include "rootston/seat.h"
#include "rootston/switch.h"

void roots_switch_handle_toggle(struct roots_switch *lid_switch,
        struct wlr_event_switch_toggle *event) {
    struct wl_list *bound_switches = &lid_switch->seat->input->server->config->switches;
    struct roots_switch_config *sc;
    wl_list_for_each(sc, bound_switches, link) {
        bool device_match = false;
        bool state_match = false;
        if ((sc->name != NULL && strcmp(event->device->name, sc->name) == 0) ||
                (sc->name == NULL && event->switch_type == sc->switch_type)) {
            device_match = true;
        }
        if (!device_match) {
            break;
        }
        if (sc->switch_state == WLR_SWITCH_STATE_TOGGLE ||
                event->switch_state == sc->switch_state) {
            state_match = true;
        }
        if (device_match && state_match) {
            execute_binding_command(lid_switch->seat, lid_switch->seat->input, sc->command);
        }
    }
}
