// Compiles message parsing, domain commands and dispatch as one unit.
// The manifest retains this sequence for the test and fuzz harnesses.
#include "../core_prelude.h"
#include "message.c"
#include "commands/config.c"
#include "commands/display.c"
#include "commands/space.c"
#include "commands/window.c"
#include "commands/query.c"
#include "commands/rule.c"
#include "commands/signal.c"
#include "message_loop.c"
