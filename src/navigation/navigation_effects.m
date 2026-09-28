// Compiles navigation scheduling and its display/window effects together.
// Their callbacks share a bounded set of event-loop hooks.
#include "../core_prelude.h"
#include "admission.c"
#include "topology.c"
#include "activation.c"
#include "../effects/display.m"
#include "../effects/window_fade.c"
#include "../effects/snapshot.m"
#include "step.c"
#include "schedule.c"
#include "command.c"
