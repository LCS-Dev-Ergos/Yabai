// Insertion feedback windows and their WindowServer registration.
// Runs on the event-loop thread.

#define INSERT_FEEDBACK_WIDTH 2
#define INSERT_FEEDBACK_RADIUS 9
void insert_feedback_show(struct window_node *node)
{
    CFTypeRef frame_region;
    CGRect frame = {{node->area.x, node->area.y},{node->area.w, node->area.h}};
    CGSNewRegionWithRect(&frame, &frame_region);
    frame.origin.x = 0; frame.origin.y = 0;

    if (!node->feedback_window.id) {
        uint64_t tags = (1ULL << 1) | (1ULL << 9);
        CFTypeRef empty_region = CGRegionCreateEmptyRegion();
        SLSNewWindowWithOpaqueShapeAndContext(g_connection, 2, frame_region, empty_region, 13, &tags, 0, 0, 64, &node->feedback_window.id, NULL);
        CFRelease(empty_region);

        sls_window_disable_shadow(node->feedback_window.id);
        SLSSetWindowResolution(g_connection, node->feedback_window.id, 1.0f);
        SLSSetWindowOpacity(g_connection, node->feedback_window.id, 0);
        SLSSetWindowLevel(g_connection, node->feedback_window.id, window_level(node->window_order[0]));
        SLSSetWindowSubLevel(g_connection, node->feedback_window.id, window_sub_level(node->window_order[0]));
        node->feedback_window.context = SLWindowContextCreate(g_connection, node->feedback_window.id, 0);
        CGContextSetLineWidth(node->feedback_window.context, INSERT_FEEDBACK_WIDTH);
        CGContextSetRGBFillColor(node->feedback_window.context,
                                   g_window_manager.insert_feedback_color.r,
                                   g_window_manager.insert_feedback_color.g,
                                   g_window_manager.insert_feedback_color.b,
                                   g_window_manager.insert_feedback_color.a*0.25f);
        CGContextSetRGBStrokeColor(node->feedback_window.context,
                                   g_window_manager.insert_feedback_color.r,
                                   g_window_manager.insert_feedback_color.g,
                                   g_window_manager.insert_feedback_color.b,
                                   g_window_manager.insert_feedback_color.a);
        SLSDisableUpdate(g_connection);
        CGContextClearRect(node->feedback_window.context, frame);
        CGContextFlush(node->feedback_window.context);
        SLSReenableUpdate(g_connection);
        SLSOrderWindow(g_connection, node->feedback_window.id, 1, node->window_order[0]);
        table_add(&g_window_manager.insert_feedback, &node->window_order[0], node);
        if (!workspace_is_macos_sequoia() && !workspace_is_macos_tahoe() && !workspace_is_macos_goldengate()) {
            update_window_notifications();
        }
    }

    CGFloat clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;
    CGFloat midx = CGRectGetMidX(frame);
    CGFloat midy = CGRectGetMidY(frame);

    switch (node->insert_dir) {
    case DIR_NORTH: {
        clip_x = -0.5f * INSERT_FEEDBACK_WIDTH;
        clip_y = midy - 0.5f * INSERT_FEEDBACK_WIDTH;
        clip_w = INSERT_FEEDBACK_WIDTH;
        clip_h = INSERT_FEEDBACK_WIDTH;
    } break;
    case DIR_EAST: {
        clip_x = midx - 0.5f * INSERT_FEEDBACK_WIDTH;
        clip_y = -0.5f * INSERT_FEEDBACK_WIDTH;
        clip_w = INSERT_FEEDBACK_WIDTH;
        clip_h = INSERT_FEEDBACK_WIDTH;
    } break;
    case DIR_SOUTH: {
        clip_x = -0.5f * INSERT_FEEDBACK_WIDTH;
        clip_y = -0.5f * INSERT_FEEDBACK_WIDTH;
        clip_w = INSERT_FEEDBACK_WIDTH;
        clip_h = -midy + INSERT_FEEDBACK_WIDTH;
    } break;
    case DIR_WEST: {
        clip_x = -0.5f * INSERT_FEEDBACK_WIDTH;
        clip_y = -0.5f * INSERT_FEEDBACK_WIDTH;
        clip_w = -midx + INSERT_FEEDBACK_WIDTH;
        clip_h = INSERT_FEEDBACK_WIDTH;
    } break;
    case STACK: {
        clip_x = -0.5f * INSERT_FEEDBACK_WIDTH;
        clip_y = -0.5f * INSERT_FEEDBACK_WIDTH;
        clip_w = INSERT_FEEDBACK_WIDTH;
        clip_h = INSERT_FEEDBACK_WIDTH;
    } break;
    }

    CGRect rect = (CGRect) {{ 0.5f*INSERT_FEEDBACK_WIDTH, 0.5f*INSERT_FEEDBACK_WIDTH }, { frame.size.width - INSERT_FEEDBACK_WIDTH, frame.size.height - INSERT_FEEDBACK_WIDTH }};
    CGRect fill = CGRectInset(rect, 0.5f*INSERT_FEEDBACK_WIDTH, 0.5f*INSERT_FEEDBACK_WIDTH);
    CGRect clip = { { rect.origin.x + clip_x, rect.origin.y + clip_y }, { rect.size.width + clip_w, rect.size.height + clip_h } };
    CGPathRef path = CGPathCreateWithRoundedRect(rect, cgrect_clamp_x_radius(rect, INSERT_FEEDBACK_RADIUS), cgrect_clamp_y_radius(rect, INSERT_FEEDBACK_RADIUS), NULL);

    SLSDisableUpdate(g_connection);
    SLSSetWindowShape(g_connection, node->feedback_window.id, 0.0f, 0.0f, frame_region);
    CGContextClearRect(node->feedback_window.context, frame);
    CGContextClipToRect(node->feedback_window.context, clip);
    CGContextFillRect(node->feedback_window.context, fill);
    CGContextAddPath(node->feedback_window.context, path);
    CGContextStrokePath(node->feedback_window.context);
    CGContextResetClip(node->feedback_window.context);
    CGContextFlush(node->feedback_window.context);
    SLSReenableUpdate(g_connection);
    CGPathRelease(path);
    CFRelease(frame_region);
}

void insert_feedback_destroy(struct window_node *node)
{
    if (node->feedback_window.id) {
        table_remove(&g_window_manager.insert_feedback, &node->window_order[0]);

        if (!workspace_is_macos_sequoia() && !workspace_is_macos_tahoe() && !workspace_is_macos_goldengate()) {
            update_window_notifications();
        }

        SLSOrderWindow(g_connection, node->feedback_window.id, 0, 0);
        CGContextRelease(node->feedback_window.context);
        SLSReleaseWindow(g_connection, node->feedback_window.id);
        memset(&node->feedback_window, 0, sizeof(struct feedback_window));
    }
}
