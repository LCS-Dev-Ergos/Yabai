// Window moves to Spaces through the Dock payload or SkyLight.
// Runs on the event-loop thread.

// The private class is loaded at runtime. Its initializer follows the usual
// Objective-C init ownership rule, which a raw objc_msgSend cast obscures.
@protocol YabaiBridgedMoveOperation
- (id)initWithWindows:(id)windows spaceID:(uint64_t)sid;
@end

// WindowManager in macOS 27.3 (first seen in 26C5047e) rejects every bridged
// operation from a client, although SkyLight still exports the entry point.
// shortcut: assumes every later release keeps rejecting, revisit once
// WindowManager accepts the operation again.
static bool space_manager_bridged_moves_rejected(void)
{
    NSOperatingSystemVersion version = { .majorVersion = 27, .minorVersion = 3 };
    return [[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:version];
}

// True when the move took effect before returning, so the caller can check
// membership now; false when WindowManager applies a bridged move later.
bool space_manager_move_window_list_to_space(uint64_t sid, uint32_t *window_list, int window_count)
{
    // Dock's connection can still move windows where the bridge is rejected.
    bool workaround = workspace_use_macos_space_workaround();
    if (workaround && scripting_addition_move_window_list_to_space(sid, window_list, window_count)) return true;

    if (SLSPerformAsynchronousBridgedWindowManagementOperation && !space_manager_bridged_moves_rejected()) {
        CFArrayRef window_list_ref = cfarray_of_cfnumbers(window_list, sizeof(uint32_t), window_count, kCFNumberSInt32Type);
        Class cls = objc_getClass("SLSBridgedMoveWindowsToManagedSpaceOperation");
        id operation = [(id<YabaiBridgedMoveOperation>)[cls alloc] initWithWindows:(__bridge id)window_list_ref spaceID:sid];
        SLSPerformAsynchronousBridgedWindowManagementOperation(operation);
        [operation release];
        CFRelease(window_list_ref);
        return false;
    }

    if (!workaround) {
        CFArrayRef window_list_ref = cfarray_of_cfnumbers(window_list, sizeof(uint32_t), window_count, kCFNumberSInt32Type);
        SLSMoveWindowsToManagedSpace(g_connection, window_list_ref, sid);
        CFRelease(window_list_ref);
    } else {
        SLSSpaceSetCompatID(g_connection, sid, 0x79616265);
        SLSSetWindowListWorkspace(g_connection, window_list, window_count, 0x79616265);
        SLSSpaceSetCompatID(g_connection, sid, 0x0);
    }

    return true;
}

bool space_manager_move_window_to_space(uint64_t sid, struct window *window)
{
    return space_manager_move_window_list_to_space(sid, &window->id, 1);
}
