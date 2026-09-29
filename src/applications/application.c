#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#ifdef YABAI_TEST_AX_OBSERVATION
#define AXUIElementSetMessagingTimeout test_ax_set_messaging_timeout
#define AXObserverCreate test_ax_observer_create
#define AXObserverAddNotification test_ax_observer_add_notification
#define AXObserverRemoveNotification test_ax_observer_remove_notification
#define AXObserverGetRunLoopSource test_ax_observer_get_run_loop_source
#endif
#ifdef YABAI_CAPTURE_DIAGNOSTICS
#include <os/signpost.h>
static void application_ax_diag(const char *phase, pid_t pid, int notification,
                                AXError result, uint64_t duration)
{
    static os_log_t log;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ log = os_log_create("com.lcs.yabai", "effects"); });
    os_signpost_event_emit(log, OS_SIGNPOST_ID_EXCLUSIVE, "ax_diag",
                           "%{public}s pid %d notification %d result %d duration_ns %llu ns %llu",
                           phase, pid, notification, result, (unsigned long long)duration,
                           (unsigned long long)clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
}
#endif
static OBSERVER_CALLBACK(application_notification_handler)
{
    if (CFEqual(notification, kAXCreatedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_CREATED, (void *) CFRetain(element), 0);
    } else if (CFEqual(notification, kAXFocusedWindowChangedNotification)) {
        __atomic_store_n(&__pending_window_focus, true, __ATOMIC_RELEASE);
        uint32_t window_id = ax_window_id(element);
        window_focus_note(window_id);
        event_loop_post(&g_event_loop, WINDOW_FOCUSED, (void *)(intptr_t) window_id, 0);
    } else if (CFEqual(notification, kAXWindowMovedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_MOVED, (void *)(intptr_t) ax_window_id(element), 0);
    } else if (CFEqual(notification, kAXWindowResizedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_RESIZED, (void *)(intptr_t) ax_window_id(element), 0);
    } else if (CFEqual(notification, kAXTitleChangedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_TITLE_CHANGED, (void *)(intptr_t) ax_window_id(element), 0);
    } else if (CFEqual(notification, kAXMenuOpenedNotification)) {
        event_loop_post(&g_event_loop, MENU_OPENED, (void *)(intptr_t) ax_window_id(element), 0);
    } else if (CFEqual(notification, kAXMenuClosedNotification)) {
        event_loop_post(&g_event_loop, MENU_CLOSED, NULL, 0);
    } else if (CFEqual(notification, kAXWindowMiniaturizedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_MINIMIZED, context, 0);
    } else if (CFEqual(notification, kAXWindowDeminiaturizedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_DEMINIMIZED, context, 0);
    } else if (CFEqual(notification, kAXUIElementDestroyedNotification)) {
        struct window *window = context;

        //
        // NOTE(asmvik): Flag events that are already queued, but not yet processed,
        // so that they will be ignored; the memory we allocated is still valid and will
        // be freed when this event is handled.
        //

        if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, NULL)) return;

        event_loop_post(&g_event_loop, WINDOW_DESTROYED, window, 0);
    }
}
#pragma clang diagnostic pop

bool application_observe(struct application *application)
{
    // AX calls to an unresponsive helper must not hold the event loop for
    // the system default timeout on every notification.
    AXError timeout_result = AXUIElementSetMessagingTimeout(application->ref, 0.25f);
    if (timeout_result != kAXErrorSuccess) {
        application->ax_retry = timeout_result == kAXErrorCannotComplete;
        debug("%s: could not set AX timeout for application '%s': %d\n", __FUNCTION__, application->name, timeout_result);
        return false;
    }
#ifdef YABAI_CAPTURE_DIAGNOSTICS
    uint64_t create_begin = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#endif
    AXError create_result = AXObserverCreate(application->pid, application_notification_handler, &application->observer_ref);
#ifdef YABAI_CAPTURE_DIAGNOSTICS
    application_ax_diag("observer_create", application->pid, -1, create_result,
                        clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - create_begin);
#endif
    if (create_result == kAXErrorSuccess) {
        application->is_observing = true;
        for (int i = 0; i < array_count(ax_application_notification); ++i) {
#ifdef YABAI_CAPTURE_DIAGNOSTICS
            uint64_t add_begin = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#endif
            AXError result = AXObserverAddNotification(application->observer_ref, application->ref, ax_application_notification[i], application);
#ifdef YABAI_CAPTURE_DIAGNOSTICS
            application_ax_diag("notification_add", application->pid, i, result,
                                clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - add_begin);
#endif
            if (result == kAXErrorSuccess || result == kAXErrorNotificationAlreadyRegistered) {
                application->notification |= 1 << i;
            } else {
                if (result == kAXErrorCannotComplete) application->ax_retry = true;
                debug("%s: error '%s' for application '%s' and notification '%s'\n", __FUNCTION__, ax_error_str[-result], application->name, ax_application_notification_str[i]);
                if (result == kAXErrorCannotComplete) break;
            }
        }

    } else if (create_result == kAXErrorCannotComplete) {
        application->ax_retry = true;
    }

    bool complete = (application->notification & AX_APPLICATION_ALL) == AX_APPLICATION_ALL;
    if (!application->is_observing || complete) {
        // On partial failure, leave the short timeout in place until the
        // caller removes registrations. Those removals may also contact AX.
        AXError reset_result = AXUIElementSetMessagingTimeout(application->ref, 0);
        if (reset_result != kAXErrorSuccess) {
            application->ax_retry |= reset_result == kAXErrorCannotComplete;
            debug("%s: could not restore AX timeout for application '%s': %d\n", __FUNCTION__, application->name, reset_result);
            return false;
        }
    }
    if (complete) {
        CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(application->observer_ref), kCFRunLoopDefaultMode);
    }

    return complete;
}

void application_unobserve(struct application *application)
{
    if (application->is_observing) {
        AXError timeout_result = AXUIElementSetMessagingTimeout(application->ref, 0.25f);
        if (timeout_result != kAXErrorSuccess) {
            debug("%s: could not set AX cleanup timeout for application '%s': %d\n", __FUNCTION__, application->name, timeout_result);
        }
        for (int i = 0; i < array_count(ax_application_notification); ++i) {
            if (!(application->notification & (1 << i))) continue;

            AXObserverRemoveNotification(application->observer_ref, application->ref, ax_application_notification[i]);
            application->notification &= ~(1 << i);
        }

        AXError reset_result = AXUIElementSetMessagingTimeout(application->ref, 0);
        if (reset_result != kAXErrorSuccess) {
            debug("%s: could not restore AX timeout for application '%s' during cleanup: %d\n", __FUNCTION__, application->name, reset_result);
        }
        application->is_observing = false;
        CFRunLoopSourceInvalidate(AXObserverGetRunLoopSource(application->observer_ref));
        CFRelease(application->observer_ref);
    }
}

uint32_t application_main_window(struct application *application)
{
    CFTypeRef window_ref = NULL;
    AXUIElementCopyAttributeValue(application->ref, kAXMainWindowAttribute, &window_ref);
    if (!window_ref) return 0;

    uint32_t window_id = ax_window_id(window_ref);
    CFRelease(window_ref);

    return window_id;
}

uint32_t application_focused_window(struct application *application)
{
    CFTypeRef window_ref = NULL;
    AXUIElementCopyAttributeValue(application->ref, kAXFocusedWindowAttribute, &window_ref);
    if (!window_ref) return 0;

    uint32_t window_id = ax_window_id(window_ref);
    CFRelease(window_ref);

    return window_id;
}

bool application_is_frontmost(struct application *application)
{
    ProcessSerialNumber psn = {0};
    _SLPSGetFrontProcess(&psn);
    return psn_equals(&psn, &application->psn);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
bool application_is_hidden(struct application *application)
{
    return IsProcessVisible(&application->psn) == 0;
}
#pragma clang diagnostic pop

CFArrayRef application_window_list(struct application *application)
{
    CFTypeRef window_list_ref = NULL;
    AXUIElementCopyAttributeValue(application->ref, kAXWindowsAttribute, &window_list_ref);
    return window_list_ref;
}

struct application *application_create(struct process *process)
{
    struct application *application = malloc(sizeof(struct application));
    memset(application, 0, sizeof(struct application));

    application->ref = AXUIElementCreateApplication(process->pid);
    application->psn = process->psn;
    application->pid = process->pid;
    application->name = process->name;
    application->is_hidden = application_is_hidden(application);
    SLSGetConnectionIDForPSN(g_connection, &application->psn, &application->connection);

    return application;
}

void application_destroy(struct application *application)
{
    CFRelease(application->ref);
    free(application);
}
#ifdef YABAI_TEST_AX_OBSERVATION
#undef AXUIElementSetMessagingTimeout
#undef AXObserverCreate
#undef AXObserverAddNotification
#undef AXObserverRemoveNotification
#undef AXObserverGetRunLoopSource
#endif
