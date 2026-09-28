// Labels owned by the space manager for individual Spaces.
// Runs on the event-loop thread.

struct space_label *space_manager_get_label_for_space(struct space_manager *sm, uint64_t sid)
{
    for (int i = 0; i < buf_len(sm->labels); ++i) {
        struct space_label *space_label = &sm->labels[i];
        if (space_label->sid == sid) {
            return space_label;
        }
    }

    return NULL;
}

struct space_label *space_manager_get_space_for_label(struct space_manager *sm, char *label)
{
    for (int i = 0; i < buf_len(sm->labels); ++i) {
        struct space_label *space_label = &sm->labels[i];
        if (string_equals(label, space_label->label)) {
            return space_label;
        }
    }

    return NULL;
}

bool space_manager_remove_label_for_space(struct space_manager *sm, uint64_t sid)
{
    for (int i = 0; i < buf_len(sm->labels); ++i) {
        struct space_label *space_label = &sm->labels[i];
        if (space_label->sid == sid) {
            free(space_label->label);
            buf_del(sm->labels, i);
            return true;
        }
    }

    return false;
}

void space_manager_set_label_for_space(struct space_manager *sm, uint64_t sid, char *label)
{
    space_manager_remove_label_for_space(sm, sid);

    for (int i = 0; i < buf_len(sm->labels); ++i) {
        struct space_label *space_label = &sm->labels[i];
        if (string_equals(space_label->label, label)) {
            free(space_label->label);
            buf_del(sm->labels, i);
            break;
        }
    }

    buf_push(sm->labels, ((struct space_label) {
        .sid   = sid,
        .label = label
    }));
}
