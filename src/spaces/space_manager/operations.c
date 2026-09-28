// Space creation, focus, swapping and display moves.
// Runs on the event-loop thread.

static inline uint64_t space_manager_find_first_user_space_for_display(uint32_t did)
{
    int count;
    uint64_t *space_list = display_space_list(did, &count);
    if (!space_list) return 0;

    for (int i = 0; i < count; ++i) {
        uint64_t sid = space_list[i];

        if (space_is_user(sid)) {
            return sid;
        }
    }

    return 0;
}

static inline bool space_manager_is_space_last_user_space(uint64_t sid)
{
    bool result = true;

    int count;
    uint64_t *space_list = display_space_list(space_display_id(sid), &count);
    if (!space_list) return true;

    for (int i = 0; i < count; ++i) {
        uint64_t c_sid = space_list[i];
        if (sid == c_sid) continue;

        if (space_is_user(c_sid)) {
            result = false;
            break;
        }
    }

    return result;
}

static enum space_op_error space_manager_swap_space_with_space_on_display(uint32_t a_did, uint64_t a_sid, uint32_t b_did, uint64_t b_sid)
{
    if (display_manager_display_is_animating(a_did)) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;
    if (display_manager_display_is_animating(b_did)) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    float window_animation_duration = g_window_manager.window_animation_duration;
    g_window_manager.window_animation_duration = 0.0f;
    __asm__ __volatile__ ("" ::: "memory");

    int a_window_list_count = 0;
    uint32_t *a_window_list = space_window_list(a_sid, &a_window_list_count, true);

    int b_window_list_count = 0;
    uint32_t *b_window_list = space_window_list(b_sid, &b_window_list_count, true);

    struct view *a_view = table_find(&g_space_manager.view, &a_sid);
    struct view *b_view = table_find(&g_space_manager.view, &b_sid);

    table_remove(&g_space_manager.view, &a_sid);
    table_remove(&g_space_manager.view, &b_sid);

    a_view->sid = b_sid;
    b_view->sid = a_sid;

    CFStringRef tmp = a_view->uuid;
    a_view->uuid    = b_view->uuid;
    b_view->uuid    = tmp;

    table_add(&g_space_manager.view, &a_sid, b_view);
    table_add(&g_space_manager.view, &b_sid, a_view);

    if (a_window_list_count) {
        space_manager_move_window_list_to_space(b_sid, a_window_list, a_window_list_count);
    }

    if (b_window_list_count) {
        space_manager_move_window_list_to_space(a_sid, b_window_list, b_window_list_count);
    }

    for (int i = 0; i < buf_len(g_space_manager.labels); ++i) {
        struct space_label *label = &g_space_manager.labels[i];
        if      (label->sid == a_sid) label->sid = b_sid;
        else if (label->sid == b_sid) label->sid = a_sid;
    }

    view_update(a_view);
    view_update(b_view);

    view_flush(a_view);
    view_flush(b_view);

    __asm__ __volatile__ ("" ::: "memory");
    g_window_manager.window_animation_duration = window_animation_duration;
    return SPACE_OP_ERROR_SUCCESS;
}

enum space_op_error space_manager_swap_space_with_space(uint64_t acting_sid, uint64_t selector_sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;

    uint32_t acting_did = space_display_id(acting_sid);
    uint32_t selector_did = space_display_id(selector_sid);

    if (acting_sid == selector_sid) return SPACE_OP_ERROR_SAME_SPACE;
    if (acting_did != selector_did) return space_manager_swap_space_with_space_on_display(acting_did, acting_sid, selector_did, selector_sid);

    bool is_animating = display_manager_display_is_animating(acting_did);
    if (is_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    uint64_t acting_prev_sid = space_manager_prev_space(acting_sid);
    uint64_t selector_prev_sid = space_manager_prev_space(selector_sid);

    uint32_t acting_prev_did = acting_prev_sid ? space_display_id(acting_prev_sid) : 0;
    uint32_t selector_prev_did = selector_prev_sid ? space_display_id(selector_prev_sid) : 0;

    bool acting_sid_is_first = !acting_prev_sid || acting_prev_did != acting_did;
    bool selector_sid_is_first = !selector_prev_sid || selector_prev_did != selector_did;

    int acting_mci = space_manager_mission_control_index(acting_sid);
    int selector_mci = space_manager_mission_control_index(selector_sid);
    bool success = true;

    if (acting_sid_is_first && !selector_sid_is_first && selector_mci - acting_mci == 1) {
        success = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
    } else if (!acting_sid_is_first && selector_sid_is_first && acting_mci - selector_mci == 1) {
        success = scripting_addition_move_space_after_space(selector_sid, acting_sid, selector_sid == space_manager_active_space());
    } else if (acting_sid_is_first && !selector_sid_is_first) {
        success  = scripting_addition_move_space_after_space(selector_sid, acting_sid, false);
        success &= scripting_addition_move_space_after_space(acting_sid, selector_prev_sid, acting_sid == space_manager_active_space());
    } else if (!acting_sid_is_first && selector_sid_is_first) {
        success  = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
        success &= scripting_addition_move_space_after_space(selector_sid, acting_prev_sid, false);
    } else if (!acting_sid_is_first && !selector_sid_is_first) {
        if (acting_mci > selector_mci) {
            success  = scripting_addition_move_space_after_space(selector_sid, acting_sid, false);
            success &= scripting_addition_move_space_after_space(acting_sid, selector_prev_sid, acting_sid == space_manager_active_space());
        } else {
            success  = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
            success &= scripting_addition_move_space_after_space(selector_sid, acting_prev_sid, false);
        }
    }

    return success ? SPACE_OP_ERROR_SUCCESS : SPACE_OP_ERROR_SCRIPTING_ADDITION;
}

enum space_op_error space_manager_move_space_to_space(uint64_t acting_sid, uint64_t selector_sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;

    uint32_t acting_did = space_display_id(acting_sid);
    uint32_t selector_did = space_display_id(selector_sid);

    if (acting_sid == selector_sid) return SPACE_OP_ERROR_SAME_SPACE;
    if (acting_did != selector_did) return SPACE_OP_ERROR_SAME_DISPLAY;

    bool is_animating = display_manager_display_is_animating(acting_did);
    if (is_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    uint64_t acting_prev_sid = space_manager_prev_space(acting_sid);
    uint64_t selector_prev_sid = space_manager_prev_space(selector_sid);

    uint32_t acting_prev_did = acting_prev_sid ? space_display_id(acting_prev_sid) : 0;
    uint32_t selector_prev_did = selector_prev_sid ? space_display_id(selector_prev_sid) : 0;

    bool acting_sid_is_first = !acting_prev_sid || acting_prev_did != acting_did;
    bool selector_sid_is_first = !selector_prev_sid || selector_prev_did != selector_did;
    bool success = true;

    if (acting_sid_is_first && !selector_sid_is_first) {
        success = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
    } else if (!acting_sid_is_first && selector_sid_is_first) {
        success  = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
        success &= scripting_addition_move_space_after_space(selector_sid, acting_sid, false);
    } else if (!acting_sid_is_first && !selector_sid_is_first) {
        if (space_manager_mission_control_index(acting_sid) > space_manager_mission_control_index(selector_sid)) {
            success = scripting_addition_move_space_after_space(acting_sid, selector_prev_sid, acting_sid == space_manager_active_space());
        } else {
            success = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
        }
    }

    return success ? SPACE_OP_ERROR_SUCCESS : SPACE_OP_ERROR_SCRIPTING_ADDITION;
}

//
// NOTE: The user space shown in place of sid when sid leaves its display, looked up on
// that display. space_manager_prev_space crosses into the previous display when sid is the
// first space of its display, which would give the source display a foreign current space.
//

static uint64_t space_manager_replacement_space(uint32_t did, uint64_t sid)
{
    int count = 0;
    uint64_t *space_list = display_space_list(did, &count);
    if (!space_list) return 0;

    int index = -1;
    for (int i = 0; i < count; ++i) {
        if (space_list[i] == sid) {
            index = i;
            break;
        }
    }

    if (index == -1) return 0;

    for (int i = index - 1; i >= 0; --i) {
        if (space_is_user(space_list[i])) return space_list[i];
    }

    for (int i = index + 1; i < count; ++i) {
        if (space_is_user(space_list[i])) return space_list[i];
    }

    return 0;
}

enum space_op_error space_manager_move_space_to_display(struct space_manager *sm, uint64_t sid, uint32_t did)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;
    if (!sid)     return SPACE_OP_ERROR_MISSING_SRC;

    uint32_t s_did = space_display_id(sid);
    if (s_did == did) return SPACE_OP_ERROR_INVALID_DST;

    bool is_src_animating = display_manager_display_is_animating(s_did);
    if (is_src_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    bool last_space = space_manager_is_space_last_user_space(sid);
    if (last_space) return SPACE_OP_ERROR_INVALID_SRC;

    bool is_dst_animating = display_manager_display_is_animating(did);
    if (is_dst_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    uint64_t d_sid = display_space_id(did);
    if (!d_sid) return SPACE_OP_ERROR_MISSING_DST;

    bool focus_space = sid == space_manager_active_space();

    //
    // NOTE: A space visible on its display, focused or not, is replaced there before it moves.
    //

    uint64_t replacement_sid = 0;
    if (space_is_visible(sid)) {
        replacement_sid = space_manager_replacement_space(s_did, sid);
        if (!replacement_sid) return SPACE_OP_ERROR_INVALID_SRC;
    }

    if (scripting_addition_move_space_to_display(sid, d_sid, replacement_sid, focus_space)) {
        space_manager_mark_view_invalid(sm, sid);
        if (focus_space) {
            space_manager_focus_space(sid);
        }
        return SPACE_OP_ERROR_SUCCESS;
    }

    return SPACE_OP_ERROR_SCRIPTING_ADDITION;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
bool space_manager_focus_space_using_gesture(uint32_t new_did, uint64_t new_sid)
{
    int cur_index = space_manager_mission_control_index(display_space_id(new_did));
    int new_index = space_manager_mission_control_index(new_sid);

    int count = abs(new_index - cur_index);
    if (count == 0) {
        display_manager_focus_display(new_did, new_sid);
        return true;
    }

    CGPoint point = display_center(new_did);
    uint32_t cur_did = display_manager_cursor_display_id();

    bool focus_display = cur_did != new_did;
    if (focus_display) CGWarpMouseCursorPosition(point);

    //
    // NOTE(asmvik): MacOS does not have an API that allows for space activation.
    // However, we can synthesize a sequence of high velocity gestures to skip the
    // animation instead.
    //
    // :Attribution
    // https://github.com/jurplel/InstantSpaceSwitcher
    // https://github.com/thenickdude/wacom-driver-fix/blob/bdfda9a788934c88d09d31ea6a42664b9ba1471e/Readme.md
    // Technique first observed in practice, and reverse-engineered from, BetterTouchTool.
    //

    CGEventRef event_dock_control = CGEventCreate(NULL);
    if (!event_dock_control) return false;

    float sign = (new_index - cur_index) > 0 ? 1.0 : -1.0;
    CGEventSetIntegerValueField(event_dock_control, /* kCGSEventTypeField            */  55, /* kCGSEventDockControl       */ 30);
    CGEventSetIntegerValueField(event_dock_control, /* kCGEventGestureHIDType        */ 110, /* kIOHIDEventTypeDockSwipe   */ 23);
    CGEventSetIntegerValueField(event_dock_control, /* kCGEventGestureSwipeMotion    */ 123, /* kCGGestureMotionHorizontal */  1);
    CGEventSetDoubleValueField(event_dock_control,  /* kCGEventGestureSwipeProgress  */ 124, sign);
    CGEventSetDoubleValueField(event_dock_control,  /* kCGEventGestureSwipeVelocityX */ 129, sign * 9999.0);

    for (int i = 0; i < count; ++i) {
        CGEventSetIntegerValueField(event_dock_control, /* kCGEventGesturePhase */ 132, /* kCGSGesturePhaseBegan */ 1);
        CGEventPost(kCGSessionEventTap, event_dock_control);
        CGEventSetIntegerValueField(event_dock_control, /* kCGEventGesturePhase */ 132, /* kCGSGesturePhaseEnded */ 4);
        CGEventPost(kCGSessionEventTap, event_dock_control);
    }
    CFRelease(event_dock_control);

    if (focus_display) {
        display_manager_set_active_display_id(new_did);
        if (space_manager_active_space() != new_sid) {
            CGPostMouseEvent(point, false, 1, true);
            CGPostMouseEvent(point, false, 1, false);
        }
    }

    return true;
}
#pragma clang diagnostic pop

enum space_op_error space_manager_focus_space(uint64_t sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;

    uint64_t cur_sid = space_manager_active_space();
    if (cur_sid == sid) return SPACE_OP_ERROR_SAME_SPACE;

    uint32_t cur_did = space_display_id(cur_sid);
    uint32_t new_did = space_display_id(sid);
    bool focus_display = cur_did != new_did;

    bool is_animating = display_manager_display_is_animating(new_did);
    if (is_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    if (scripting_addition_focus_space(sid)) {
        if (focus_display) {
            display_manager_focus_display(new_did, sid);
        }
    } else {
        space_manager_focus_space_using_gesture(new_did, sid);
    }

    return SPACE_OP_ERROR_SUCCESS;
}

enum space_op_error space_manager_switch_space(uint64_t sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;

    uint64_t cur_sid = space_manager_active_space();
    if (cur_sid == sid) return SPACE_OP_ERROR_SAME_SPACE;

    uint32_t cur_did = space_display_id(cur_sid);
    uint32_t did     = space_display_id(sid);

    bool is_src_animating = display_manager_display_is_animating(cur_did);
    if (is_src_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    bool is_dst_animating = display_manager_display_is_animating(did);
    if (is_dst_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    if (cur_did != did) {
        space_manager_swap_space_with_space_on_display(cur_did, cur_sid, did, sid);
        display_manager_focus_display(cur_did, cur_sid);
        return SPACE_OP_ERROR_SUCCESS;
    }

    return scripting_addition_focus_space(sid) ? SPACE_OP_ERROR_SUCCESS : SPACE_OP_ERROR_SCRIPTING_ADDITION;
}

enum space_op_error space_manager_destroy_space(uint64_t sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;

    if (!sid) return SPACE_OP_ERROR_MISSING_SRC;
    if (!space_is_user(sid)) return SPACE_OP_ERROR_INVALID_TYPE;
    if (space_manager_is_space_last_user_space(sid)) return SPACE_OP_ERROR_INVALID_SRC;

    uint32_t did = space_display_id(sid);
    uint64_t first_sid = space_manager_find_first_user_space_for_display(did);

    bool is_animating = display_manager_display_is_animating(did);
    if (is_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    bool success = scripting_addition_destroy_space(sid);
    if (!success) return SPACE_OP_ERROR_SCRIPTING_ADDITION;

    if (first_sid) {
        window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, first_sid);
    }

    return SPACE_OP_ERROR_SUCCESS;
}

enum space_op_error space_manager_add_space(uint64_t sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;
    if (!sid)     return SPACE_OP_ERROR_MISSING_SRC;

    bool is_animating = display_manager_display_is_animating(space_display_id(sid));
    if (is_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    return scripting_addition_create_space(sid) ? SPACE_OP_ERROR_SUCCESS : SPACE_OP_ERROR_SCRIPTING_ADDITION;
}
