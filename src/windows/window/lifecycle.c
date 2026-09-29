// Creation and release of tracked window objects.
// Runs on the event-loop thread.

struct window *window_create(struct application *application, AXUIElementRef window_ref, uint32_t window_id)
{
    struct window *window = malloc(sizeof(struct window));
    memset(window, 0, sizeof(struct window));

    window->application = application;
    window->ref = window_ref;
    window->id = window_id;
    window->id_ptr = &window->id;
    window->frame = window_ax_frame(window);
    window->role = window_ax_role(window);
    window->subrole = window_ax_subrole(window);
    window->title = window_title(window);
    window->is_root = !window_parent(window->id) || window_is_root(window);

    if (window_shadow(window->id)) {
        window_set_flag(window, WINDOW_SHADOW);
    }

    if (window_is_minimized(window)) {
        window_set_flag(window, WINDOW_MINIMIZE);
    }

    if (window_ax_can_move(window)) {
        window_set_flag(window, WINDOW_MOVABLE);
    }

    if (window_ax_can_resize(window)) {
        window_set_flag(window, WINDOW_RESIZABLE);
    }

    if ((window_is_fullscreen(window)) ||
        (space_is_fullscreen(window_space(window->id)))) {
        window_set_flag(window, WINDOW_FULLSCREEN);
    }

    if (window_is_sticky(window->id)) {
        window_set_flag(window, WINDOW_STICKY);
    }

    return window;
}

void window_destroy(struct window *window)
{
    window->id = 0;
    if (window->role) CFRelease(window->role);
    if (window->subrole) CFRelease(window->subrole);
    if (window->title) CFRelease(window->title);
    CFRelease(window->ref);
    free(window);
}
