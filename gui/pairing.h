#pragma once

#include <signal.h>
#include "status.h"

/* Mono pairing screen. Returns once pairing is complete or when *quitting becomes nonzero. */
void svrt_pairing_gui_show(svrt_status_server *server, volatile sig_atomic_t *quitting);
