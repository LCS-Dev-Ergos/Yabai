// SA_OPCODE_SPACE_FOCUS_CROSSFADE, the Desktop switch of do_space_focus
// with a crossfade (see space_crossfade.c). It replies 'k' once the
// destination is current, or 'e' when the daemon should switch without an
// effect.
static void do_space_focus_crossfade(int sockfd, char *message)
{
    uint32_t display;
    uint64_t dest_space_id;
    float duration, interval;
    unpack(display);
    unpack(dest_space_id);
    unpack(duration);
    unpack(interval);

    bool success = false;
    CFStringRef dest_display = dock_spaces != nil && dest_space_id
                             ? SLSCopyManagedDisplayForSpace(SLSMainConnectionID(), dest_space_id)
                             : NULL;

    if (dest_display) {
        id source_space = current_space_for_display(dest_display, dest_space_id);
        uint64_t source_space_id = get_space_id(source_space);
        id dest_space = space_for_display_with_id(dest_display, dest_space_id);
        id display_space = display_space_for_space_with_id(source_space_id);

        if (source_space_id == dest_space_id) {
            success = true;
        } else if (dest_space != nil && display_space != nil
                   && space_crossfade_start(display, dest_display, source_space_id, dest_space_id, duration, interval)) {
            set_ivar_value(display_space, "_currentSpace", [dest_space retain]);
            success = true;
        }

        CFRelease(dest_display);
    }

    char result = success ? 'k' : 'e';
    send(sockfd, &result, 1, 0);
}
