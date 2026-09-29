// AX roles and window classification for management.
// Runs on the event-loop thread.

CFStringRef window_ax_role(struct window *window)
{
    const void *role = NULL;
    AXUIElementCopyAttributeValue(window->ref, kAXRoleAttribute, &role);
    return role;
}

CFStringRef window_role(struct window *window)
{
    return window->role;
}

char *window_role_ts(struct window *window)
{
    CFStringRef role = window_role(window);
    if (!role) return ts_string_copy("");

    char *result = ts_cfstring_copy(role);
    return result;
}

CFStringRef window_ax_subrole(struct window *window)
{
    const void *srole = NULL;
    AXUIElementCopyAttributeValue(window->ref, kAXSubroleAttribute, &srole);
    return srole;
}

CFStringRef window_subrole(struct window *window)
{
    return window->subrole;
}

char *window_subrole_ts(struct window *window)
{
    CFStringRef subrole = window_subrole(window);
    if (!subrole) return ts_string_copy("");

    char *result = ts_cfstring_copy(subrole);
    return result;
}

static bool window_is_root(struct window *window)
{
    bool result = false;
    CFTypeRef value = NULL;

    if (AXUIElementCopyAttributeValue(window->ref, kAXParentAttribute, &value) == kAXErrorSuccess) {
        result = !(value && !CFEqual(value, window->application->ref));
    }

    if (value) CFRelease(value);
    return result;
}

bool window_is_real(struct window *window)
{
    bool win = false;
    CFStringRef role  = NULL;
    CFStringRef srole = NULL;

    if (!(role  = window_role(window)))    goto out;
    if (!(srole = window_subrole(window))) goto out;

    win = CFEqual(role, kAXWindowRole) &&
          (CFEqual(srole, kAXStandardWindowSubrole) ||
           CFEqual(srole, kAXFloatingWindowSubrole) ||
           CFEqual(srole, kAXDialogSubrole));

out:
    return win;
}

bool window_is_standard(struct window *window)
{
    bool standard_win = false;
    CFStringRef role  = NULL;
    CFStringRef srole = NULL;

    if (!(role  = window_role(window)))    goto out;
    if (!(srole = window_subrole(window))) goto out;

    standard_win = CFEqual(role, kAXWindowRole) &&
                   CFEqual(srole, kAXStandardWindowSubrole);

out:
    return standard_win;
}

bool window_level_is_standard(struct window *window)
{
    int level = window_level(window->id);
    return level == g_layer_normal_window_level;
}

bool window_is_unknown(struct window *window)
{
    CFStringRef subrole = window_subrole(window);
    if (!subrole) return false;

    bool result = CFEqual(subrole, kAXUnknownSubrole);
    return result;
}
