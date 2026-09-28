// Owns daemon globals and the single implementation of shared core helpers.
// All other daemon areas compile in their own translation units.
#define YABAI_DEFINE_CORE
#include "core_prelude.h"
#undef YABAI_DEFINE_CORE
#include "yabai.c"
