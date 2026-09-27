// Fork: focuses a window of the already active application without blocking
// the event loop.
//
// window_manager_focus_window_without_raise posts a deactivation to the
// focused window, sleeps 40 ms because some applications miss a focus change
// whose two events arrive together (upstream #2694), then activates the new
// window. The sleep held the event loop, and with it the reply and every
// queued request, on each switch between two windows of one application.
// Navigation posts the deactivation, then finishes 10 ms later as an event,
// unless a newer navigation, another command or a focus change came first.
//
// The destination window shows inactive until the activation lands, which
// reads as a title-bar flash. Upstream used 10 ms from 2019 until #2694, so
// navigation keeps that shorter spacing.

#define SPACE_NAVIGATION_FOCUS_DELAY_NS 10000000ULL

static struct
{
    int generation;
    ProcessSerialNumber psn;
    uint32_t window_id;
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

    space_navigation_focus_schedule(g_space_navigation_focus.generation, SPACE_NAVIGATION_FOCUS_DELAY_NS);
}

// Navigation's raise, for an application with a window visible on another
// display: it activates the application on the window and raises the window
// through Accessibility, which makes it key. Unlike window --focus it posts no
// synthesized click. A busy application such as Edge handled that click
// hundreds of milliseconds late, after the user had moved on; the click then
// activated it again and pulled its Desktop back into view.
static void space_navigation_raise_window(ProcessSerialNumber *psn, uint32_t window_id, AXUIElementRef ref)
{
    _SLPSSetFrontProcessWithOptions(psn, window_id, kCPSUserGenerated);
    AXUIElementPerformAction(ref, kAXRaiseAction);
}

// Event loop, once the delay has passed. A click on another application in
// the meantime wins over the navigation. Another window of this application
// becoming focused does not: it is usually an earlier navigation's activation
// that the busy application handled late, and without this activation that
// window pulls its Desktop back into view.
static void space_navigation_focus_resume(int generation)
{
    if (generation != g_space_navigation_focus.generation) return;
    if (!psn_equals(&g_window_manager.focused_window_psn, &g_space_navigation_focus.psn)) return;

    space_navigation_focus_post(&g_space_navigation_focus.psn, g_space_navigation_focus.window_id, 0x01);

    _SLPSSetFrontProcessWithOptions(&g_space_navigation_focus.psn, g_space_navigation_focus.window_id, kCPSUserGenerated);
    window_manager_make_key_window(&g_space_navigation_focus.psn, g_space_navigation_focus.window_id);
}
