// SkyLight window properties, tags and stacking levels.
// Runs on the event-loop thread.

bool window_is_sticky(uint32_t wid)
{
    bool result = false;

    CFArrayRef window_list_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);
    CFArrayRef space_list_ref = SLSCopySpacesForWindows(g_connection, 0x7, window_list_ref);
    if (!space_list_ref) goto err;

    result = CFArrayGetCount(space_list_ref) > 1;
    CFRelease(space_list_ref);

err:
    CFRelease(window_list_ref);
    return result;
}

bool window_shadow(uint32_t wid)
{
    uint64_t tags = window_tags(wid);
    return !(tags & 0x8);
}

float window_opacity(uint32_t wid)
{
    float alpha = 0.0f;
    SLSGetWindowAlpha(g_connection, wid, &alpha);
    return alpha;
}

uint32_t window_parent(uint32_t wid)
{
    uint32_t parent_wid = 0;

    CFArrayRef window_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);

    CFTypeRef query = SLSWindowQueryWindows(g_connection, window_ref, 1);
    if (!query) goto err2;

    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (!iterator) goto err1;

    if (SLSWindowIteratorGetCount(iterator) == 1) {
        if (SLSWindowIteratorAdvance(iterator)) {
            parent_wid = SLSWindowIteratorGetParentID(iterator);
        }
    }

    CFRelease(iterator);
err1:
    CFRelease(query);
err2:
    CFRelease(window_ref);

    return parent_wid;
}

int window_level(uint32_t wid)
{
    int level = 0;

    if (workspace_is_macos_ventura() || workspace_is_macos_sonoma() || workspace_is_macos_sequoia() || workspace_is_macos_tahoe() || workspace_is_macos_goldengate()) {
        CFArrayRef window_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);

        CFTypeRef query = SLSWindowQueryWindows(g_connection, window_ref, 1);
        if (!query) goto err2;

        CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
        if (!iterator) goto err1;

        if (SLSWindowIteratorGetCount(iterator) == 1) {
            if (SLSWindowIteratorAdvance(iterator)) {
                level = SLSWindowIteratorGetLevel(iterator);
            }
        }

        CFRelease(iterator);
    err1:
        CFRelease(query);
    err2:
        CFRelease(window_ref);
    } else {
        SLSGetWindowLevel(g_connection, wid, &level);
    }

    return level;
}

static int SLSGetWindowSubLevel__Internal(int cid, uint32_t wid)
{
    #pragma pack(push,4)
    struct {
        mach_msg_header_t header;
        NDR_record_t NDR_record;
        uint32_t window_id;
        int32_t sub_level;
        int32_t padding1;
        int32_t padding2;
    } msg = {0};
    #pragma pack(pop)

    msg.NDR_record = NDR_record;
    msg.window_id = wid;
    msg.header.msgh_bits = 0x1513;
    msg.header.msgh_remote_port = CGSGetConnectionPortById(cid);
    msg.header.msgh_local_port = mig_get_special_reply_port();
    msg.header.msgh_id = (workspace_is_macos_tahoe() || workspace_is_macos_goldengate()) ? 0x76E3 : 0x73C3;
    mach_msg(&msg.header, MACH_SEND_MSG|MACH_RCV_MSG, 0x24, 0x30, msg.header.msgh_local_port, 0, 0);

    return msg.sub_level;
}

int window_sub_level(uint32_t wid)
{
    if (CGSGetConnectionPortById) {
        return SLSGetWindowSubLevel__Internal(g_connection, wid);
    } else {
        return SLSGetWindowSubLevel(g_connection, wid);
    }
}

uint64_t window_tags(uint32_t wid)
{
    uint64_t tags = 0;
    CFArrayRef window_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);

    CFTypeRef query = SLSWindowQueryWindows(g_connection, window_ref, 1);
    if (!query) goto err2;

    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (!iterator) goto err1;

    if (SLSWindowIteratorGetCount(iterator) == 1) {
        if (SLSWindowIteratorAdvance(iterator)) {
            tags = SLSWindowIteratorGetTags(iterator);
        }
    }

    CFRelease(iterator);
err1:
    CFRelease(query);
err2:
    CFRelease(window_ref);

    return tags;
}
