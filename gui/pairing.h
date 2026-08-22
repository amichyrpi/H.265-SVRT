#pragma once

#include <signal.h>
#include "steam_link_pairing.h"

/* Mono pairing screen. Returns once pairing is complete or when *quitting becomes nonzero. */
void svrt_pairing_gui_show(svrt_steam_link_pairing *pairing,
                           volatile sig_atomic_t *quitting);
