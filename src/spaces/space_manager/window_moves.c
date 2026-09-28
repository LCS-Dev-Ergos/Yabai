// Window moves to Spaces through SkyLight or the Dock payload.
// Runs on the event-loop thread.

void space_manager_move_window_list_to_space(uint64_t sid, uint32_t *window_list, int window_count)
{
    if (SLSPerformAsynchronousBridgedWindowManagementOperation) {
        CFArrayRef window_list_ref = cfarray_of_cfnumbers(window_list, sizeof(uint32_t), window_count, kCFNumberSInt32Type);
        Class cls = objc_getClass("SLSBridgedMoveWindowsToManagedSpaceOperation");
        SEL sel = sel_registerName("initWithWindows:spaceID:");
        id operation = ((id (*)(id, SEL, id, uint64_t))objc_msgSend)([cls alloc], sel, (__bridge id)window_list_ref, sid);
        SLSPerformAsynchronousBridgedWindowManagementOperation(operation);
        [operation release];
        CFRelease(window_list_ref);
    } else if (!workspace_use_macos_space_workaround()) {
        CFArrayRef window_list_ref = cfarray_of_cfnumbers(window_list, sizeof(uint32_t), window_count, kCFNumberSInt32Type);
        SLSMoveWindowsToManagedSpace(g_connection, window_list_ref, sid);
        CFRelease(window_list_ref);
    } else if (!scripting_addition_move_window_list_to_space(sid, window_list, window_count)) {
        SLSSpaceSetCompatID(g_connection, sid, 0x79616265);
        SLSSetWindowListWorkspace(g_connection, window_list, window_count, 0x79616265);
        SLSSpaceSetCompatID(g_connection, sid, 0x0);
    }
}

void space_manager_move_window_to_space(uint64_t sid, struct window *window)
{
    if (SLSPerformAsynchronousBridgedWindowManagementOperation) {
        CFArrayRef window_list_ref = cfarray_of_cfnumbers(&window->id, sizeof(uint32_t), 1, kCFNumberSInt32Type);
        Class cls = objc_getClass("SLSBridgedMoveWindowsToManagedSpaceOperation");
        SEL sel = sel_registerName("initWithWindows:spaceID:");
        id operation = ((id (*)(id, SEL, id, uint64_t))objc_msgSend)([cls alloc], sel, (__bridge id)window_list_ref, sid);
        SLSPerformAsynchronousBridgedWindowManagementOperation(operation);
        [operation release];
        CFRelease(window_list_ref);
    } else if (!workspace_use_macos_space_workaround()) {
        CFArrayRef window_list_ref = cfarray_of_cfnumbers(&window->id, sizeof(uint32_t), 1, kCFNumberSInt32Type);
        SLSMoveWindowsToManagedSpace(g_connection, window_list_ref, sid);
        CFRelease(window_list_ref);
    } else if (!scripting_addition_move_window_to_space(sid, window->id)) {
        SLSSpaceSetCompatID(g_connection, sid, 0x79616265);
        SLSSetWindowListWorkspace(g_connection, &window->id, 1, 0x79616265);
        SLSSpaceSetCompatID(g_connection, sid, 0x0);
    }
}
