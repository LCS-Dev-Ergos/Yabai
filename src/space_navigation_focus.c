// Fork: focuses a window of the already active application without blocking
// the event loop.
//
// window_manager_focus_window_without_raise posts a deactivation to the
// focused window, sleeps 40 ms because some applications miss a focus change
// whose two events arrive together (upstream #2694), then activates the new
// window. The sleep held the event loop, and with it the reply and every
// queued request, on each switch between two windows of one application.
// Navigation posts the deactivation, then finishes 40 ms later as an event,
// unless a newer navigation, another command or a focus change came first.

#define SPACE_NAVIGATION_FOCUS_DELAY_NS 40000000ULL

static struct
{
    int generation;
    ProcessSerialNumber psn;
    uint32_t window_id;
    uint32_t previous_id;
} g_space_navigation_focus;

static void window_manager_make_key_window(ProcessSerialNumber *window_psn, uint32_t window_id);
static void space_navigation_focus_schedule(int generation, uint64_t delay_ns);

static void space_navigation_focus_post(ProcessSerialNumber *psn, uint32_t window_id, uint8_t kind)
{
    uint8_t bytes[0xf8] = { 0 };

    bytes[0x04] = 0xf8;
    bytes[0x08] = 0x0d;
    bytes[0x8a] = kind;
    memcpy(bytes + 0x3c, &window_id, sizeof(uint32_t));

    SLPSPostEventRecordTo(psn, bytes);
}

static void space_navigation_focus_cancel(void)
{
    ++g_space_navigation_focus.generation;
}

static void space_navigation_focus_window(ProcessSerialNumber *psn, uint32_t window_id)
{
    space_navigation_focus_cancel();

    if (!psn_equals(psn, &g_window_manager.focused_window_psn)) {
        window_manager_focus_window_without_raise(psn, window_id);
        return;
    }

    space_navigation_focus_post(&g_window_manager.focused_window_psn, g_window_manager.focused_window_id, 0x02);

    g_space_navigation_focus.psn = *psn;
    g_space_navigation_focus.window_id = window_id;
    g_space_navigation_focus.previous_id = g_window_manager.focused_window_id;

    space_navigation_focus_schedule(g_space_navigation_focus.generation, SPACE_NAVIGATION_FOCUS_DELAY_NS);
}

// Event loop, once the delay has passed. A focus change to any other window
// in the meantime, such as a click, wins over the navigation.
static void space_navigation_focus_resume(int generation)
{
    if (generation != g_space_navigation_focus.generation) return;

    uint32_t focused_id = g_window_manager.focused_window_id;
    if (focused_id != g_space_navigation_focus.previous_id && focused_id != g_space_navigation_focus.window_id) return;

    space_navigation_focus_post(&g_space_navigation_focus.psn, g_space_navigation_focus.window_id, 0x01);

    _SLPSSetFrontProcessWithOptions(&g_space_navigation_focus.psn, g_space_navigation_focus.window_id, kCPSUserGenerated);
    window_manager_make_key_window(&g_space_navigation_focus.psn, g_space_navigation_focus.window_id);
}
