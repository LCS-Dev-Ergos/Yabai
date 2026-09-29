#define YABAI_DEFINE_CORE
#include "core_prelude.h"
#undef YABAI_DEFINE_CORE

// Navigation and effects come before the core, so they can use only what the
// headers declare.
#include "navigation/admission.c"
#include "navigation/topology.c"
#include "navigation/activation.c"
#include "effects/display.m"
#include "effects/window_fade.c"
#include "effects/snapshot.m"
#include "navigation/step.c"
#include "navigation/schedule.c"
#include "navigation/command.c"

#include "sa/sa.m"
#include "events/mission_control.c"
#include "events/event_queue.c"
#include "events/event_loop.c"
#include "events/event_signal.c"
#include "events/workspace.m"
#include "windows/rule.c"
#include "ipc/message.c"
#include "ipc/commands/config.c"

#ifdef YABAI_TEST_COMMAND_STATE
static uint32_t test_command_active_display_id(void);
#define display_manager_active_display_id test_command_active_display_id
#endif

#include "ipc/commands/display.c"

#ifdef YABAI_TEST_COMMAND_STATE
#undef display_manager_active_display_id
static uint64_t test_command_active_space(void);
#define space_manager_active_space test_command_active_space
#endif

#include "ipc/commands/space.c"

#ifdef YABAI_TEST_COMMAND_STATE
#undef space_manager_active_space
static struct window *test_command_focused_window(struct window_manager *wm);
static void test_rule_make_window_floating(struct space_manager *sm, struct window_manager *wm, struct window *window, bool should_float, bool force);
#define window_manager_focused_window test_command_focused_window
#define window_manager_make_window_floating test_rule_make_window_floating
#endif

#include "ipc/commands/window.c"

#ifdef YABAI_TEST_COMMAND_STATE
#undef window_manager_focused_window
#undef window_manager_make_window_floating
#define space_manager_active_space test_command_active_space
#endif

#include "ipc/commands/query.c"
#ifdef YABAI_TEST_COMMAND_STATE
#undef space_manager_active_space
#endif
#include "ipc/commands/rule.c"
#include "ipc/commands/signal.c"
#include "ipc/message_loop.c"
#include "displays/display.c"
#include "spaces/space.c"
#include "spaces/view/feedback.c"
#include "spaces/view/geometry.c"
#include "spaces/view/nodes.c"

#ifdef YABAI_TEST_DIRECTION_WINDOWS
static uint32_t *test_direction_space_window_list(uint64_t sid, int *count, bool include_minimized);
#define space_window_list test_direction_space_window_list
#endif

#include "spaces/view/direction.c"

#ifdef YABAI_TEST_DIRECTION_WINDOWS
#undef space_window_list
#endif

#include "spaces/view/operations.c"

#ifdef YABAI_TEST_VIEW_GEOMETRY
static uint32_t test_view_space_display_id(uint64_t sid);
static CGRect test_view_display_bounds_constrained(uint32_t did, bool ignore_external_bar);
static bool test_view_space_is_visible(uint64_t sid);
#define space_display_id test_view_space_display_id
#define display_bounds_constrained test_view_display_bounds_constrained
#define space_is_visible test_view_space_is_visible
#endif

#include "spaces/view/lifecycle.c"

#ifdef YABAI_TEST_VIEW_GEOMETRY
#undef space_display_id
#undef display_bounds_constrained
#undef space_is_visible
#endif

#include "windows/window/observation.c"
#include "windows/window/space.c"
#include "windows/window/serialize_nonax.c"
#include "windows/window/serialize.c"
#include "windows/window/attributes.c"
#include "windows/window/properties.c"
#include "windows/window/identity.c"
#include "windows/window/lifecycle.c"
#include "applications/process_manager.c"
#include "applications/application.c"
#include "displays/display_manager.c"
#include "spaces/space_manager/views.c"
#include "spaces/space_manager/labels.c"
#include "spaces/space_manager/layout.c"
#include "spaces/space_manager/selectors.c"
#include "spaces/space_manager/window_moves.c"
#include "spaces/space_manager/operations.c"
#include "spaces/space_manager/state.c"
#include "windows/window_manager/tables.c"
#include "windows/window_manager/query.c"

#ifdef YABAI_TEST_RULE_EFFECTS
static void test_rule_make_window_floating(struct space_manager *sm, struct window_manager *wm, struct window *window, bool should_float, bool force);
static void test_rule_make_window_sticky(struct space_manager *sm, struct window_manager *wm, struct window *window, bool should_sticky);
#define window_manager_make_window_floating test_rule_make_window_floating
#define window_manager_make_window_sticky test_rule_make_window_sticky
#endif

#include "windows/window_manager/rules.c"

#ifdef YABAI_TEST_RULE_EFFECTS
#undef window_manager_make_window_floating
#undef window_manager_make_window_sticky
#endif

#include "windows/window_manager/frames.c"
#include "windows/window_manager/animation.c"
#include "windows/window_manager/appearance.c"
#include "windows/window_manager/lookup.c"
#include "windows/window_manager/focus.c"
#include "windows/window_manager/operations.c"
#include "windows/window_manager/scratchpad.c"
#include "windows/window_manager/spaces.c"
#include "events/mouse_handler.c"
#include "yabai.c"
