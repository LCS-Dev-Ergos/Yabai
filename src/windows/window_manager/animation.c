// Window animations: proxy windows built on short-lived threads, frames on a
// CVDisplayLink thread that also asks Dock to swap proxies, and
// JankyBorders notifications. window_animations_lock guards what those
// threads share with the event loop.

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
static inline void window_manager_notify_jankyborders(struct window_animation *animation_list, int animation_count, uint32_t event, bool skip, bool wait)
{
    mach_port_t port;
    if (g_bs_port && bootstrap_look_up(g_bs_port, "git.felix.jbevent", &port) == KERN_SUCCESS) {
        struct {
            uint32_t event;
            uint32_t count;
            uint32_t proxy_wid[512];
            uint32_t real_wid[512];
        } data = { event, 0 };

        for (int i = 0; i < animation_count; ++i) {
            if (skip && __atomic_load_n(&animation_list[i].skip, __ATOMIC_RELAXED)) continue;

            data.proxy_wid[data.count] = animation_list[i].proxy.id;
            data.real_wid[data.count]  = animation_list[i].wid;

            ++data.count;
        }

        mach_send(port, &data, sizeof(data));
        if (wait) usleep(20000);
    }
}
#pragma clang diagnostic pop

static void window_manager_create_window_proxy(int animation_connection, float alpha, struct window_proxy *proxy)
{
    if (!proxy->image) return;

    CFTypeRef frame_region;
    CGSNewRegionWithRect(&proxy->frame, &frame_region);
    CFTypeRef empty_region = CGRegionCreateEmptyRegion();

    uint64_t tags = 1ULL << 46;
    SLSNewWindowWithOpaqueShapeAndContext(animation_connection, 2, frame_region, empty_region, 13|(1 << 18), &tags, 0, 0, 64, &proxy->id, NULL);
    sls_window_disable_shadow(proxy->id);
    SLSSetWindowOpacity(animation_connection, proxy->id, 0);
    SLSSetWindowResolution(animation_connection, proxy->id, 2.0f);
    SLSSetWindowAlpha(animation_connection, proxy->id, alpha);
    SLSSetWindowLevel(animation_connection, proxy->id, proxy->level);
    SLSSetWindowSubLevel(animation_connection, proxy->id, proxy->sub_level);
    proxy->context = SLWindowContextCreate(animation_connection, proxy->id, 0);

    CGRect frame = { {0, 0}, proxy->frame.size };
    CGContextClearRect(proxy->context, frame);
    CGContextDrawImage(proxy->context, frame, proxy->image);
    CGContextFlush(proxy->context);
    CFRelease(frame_region);
    CFRelease(empty_region);
}

static void window_manager_destroy_window_proxy(int animation_connection, struct window_proxy *proxy)
{
    if (proxy->image) {
        CFRelease(proxy->image);
        proxy->image = NULL;
    }

    if (proxy->context) {
        CGContextRelease(proxy->context);
        proxy->context = NULL;
    }

    if (proxy->id) {
        SLSReleaseWindow(animation_connection, proxy->id);
        proxy->id = 0;
    }
}

static void *window_manager_build_window_proxy_thread_proc(void *data)
{
    struct window_animation *animation = data;

    float alpha = 1.0f;
    SLSGetWindowAlpha(animation->cid, animation->wid, &alpha);
    animation->proxy.level = window_level(animation->wid);
    animation->proxy.sub_level = window_sub_level(animation->wid);
    SLSGetWindowBounds(animation->cid, animation->wid, &animation->proxy.frame);
    animation->proxy.tx = animation->proxy.frame.origin.x;
    animation->proxy.ty = animation->proxy.frame.origin.y;
    animation->proxy.tw = animation->proxy.frame.size.width;
    animation->proxy.th = animation->proxy.frame.size.height;

    CFArrayRef image_array = SLSHWCaptureWindowList(animation->cid, &animation->wid, 1, (1 << 11) | (1 << 8));
    if (image_array) {
        animation->proxy.image = alpha == 1.0f
                               ? (CGImageRef) CFRetain(CFArrayGetValueAtIndex(image_array, 0))
                               : cgimage_restore_alpha((CGImageRef) CFArrayGetValueAtIndex(image_array, 0));
        CFRelease(image_array);
    } else {
        animation->proxy.image = NULL;
    }

    window_manager_create_window_proxy(animation->cid, alpha, &animation->proxy);
    return NULL;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static CVReturn window_manager_animate_window_list_thread_proc(CVDisplayLinkRef link, const CVTimeStamp *now, const CVTimeStamp *output_time, CVOptionFlags flags, CVOptionFlags *flags_out, void *data)
{
    struct window_animation_context *context = data;
    int animation_count = context->animation_count;

    uint64_t current_clock = output_time->hostTime;
    if (!context->animation_clock) context->animation_clock = now->hostTime;

    double t = (double)(current_clock - context->animation_clock) / (double)(context->animation_duration * g_cv_host_clock_frequency);
    if (t <= 0.0) t = 0.0f;
    if (t >= 1.0) t = 1.0f;

    float mt;
    switch (context->animation_easing) {
#define ANIMATION_EASING_TYPE_ENTRY(value) case value##_type: mt = value(t); break;
        ANIMATION_EASING_TYPE_LIST
#undef ANIMATION_EASING_TYPE_ENTRY
    }

    CFTypeRef transaction = SLSTransactionCreate(context->animation_connection);
    for (int i = 0; i < animation_count; ++i) {
        if (__atomic_load_n(&context->animation_list[i].skip, __ATOMIC_RELAXED)) continue;

        context->animation_list[i].proxy.tx = lerp(context->animation_list[i].proxy.frame.origin.x,    mt, context->animation_list[i].x);
        context->animation_list[i].proxy.ty = lerp(context->animation_list[i].proxy.frame.origin.y,    mt, context->animation_list[i].y);
        context->animation_list[i].proxy.tw = lerp(context->animation_list[i].proxy.frame.size.width,  mt, context->animation_list[i].w);
        context->animation_list[i].proxy.th = lerp(context->animation_list[i].proxy.frame.size.height, mt, context->animation_list[i].h);

        CGAffineTransform transform = CGAffineTransformMakeTranslation(-context->animation_list[i].proxy.tx, -context->animation_list[i].proxy.ty);
        CGAffineTransform scale = CGAffineTransformMakeScale(context->animation_list[i].proxy.frame.size.width / context->animation_list[i].proxy.tw, context->animation_list[i].proxy.frame.size.height / context->animation_list[i].proxy.th);
        SLSTransactionSetWindowTransform(transaction, context->animation_list[i].proxy.id, 0, 0, CGAffineTransformConcat(transform, scale));

        float alpha = 0.0f;
        SLSGetWindowAlpha(context->animation_connection, context->animation_list[i].wid, &alpha);
        if (alpha != 0.0f) SLSTransactionSetWindowAlpha(transaction, context->animation_list[i].proxy.id, alpha);
    }
    SLSTransactionCommit(transaction, 0);
    CFRelease(transaction);
    if (t != 1.0f) goto out;

    pthread_mutex_lock(&g_window_manager.window_animations_lock);
    SLSDisableUpdate(context->animation_connection);
    window_manager_notify_jankyborders(context->animation_list, context->animation_count, 1326, true, true);
    scripting_addition_swap_window_proxy_out(context->animation_list, context->animation_count);
    for (int i = 0; i < animation_count; ++i) {
        if (__atomic_load_n(&context->animation_list[i].skip, __ATOMIC_RELAXED)) continue;

        table_remove(&g_window_manager.window_animations_table, &context->animation_list[i].wid);
        window_manager_destroy_window_proxy(context->animation_connection, &context->animation_list[i].proxy);

    }
    SLSReenableUpdate(context->animation_connection);
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);

    SLSReleaseConnection(context->animation_connection);
    free(context->animation_list);
    free(context);

    CVDisplayLinkStop(link);
    CVDisplayLinkRelease(link);

out:
    return kCVReturnSuccess;
}
#pragma clang diagnostic pop

static void window_manager_animate_window_list_async(struct window_capture *window_list, int window_count)
{
    struct window_animation_context *context = malloc(sizeof(struct window_animation_context));

    SLSNewConnection(0, &context->animation_connection);
    context->animation_count    = window_count;
    context->animation_list     = malloc(window_count * sizeof(struct window_animation));
    context->animation_duration = g_window_manager.window_animation_duration;
    context->animation_easing   = g_window_manager.window_animation_easing;
    context->animation_clock    = 0;

    int thread_count = 0;
    pthread_t *threads = ts_alloc_list(pthread_t, window_count);

    TIME_BODY(window_manager_animate_window_list_async___prep_proxies, {
    SLSDisableUpdate(context->animation_connection);
    pthread_mutex_lock(&g_window_manager.window_animations_lock);
    for (int i = 0; i < window_count; ++i) {
        context->animation_list[i].window = window_list[i].window;
        context->animation_list[i].wid    = window_list[i].window->id;
        context->animation_list[i].x      = window_list[i].x;
        context->animation_list[i].y      = window_list[i].y;
        context->animation_list[i].w      = window_list[i].w;
        context->animation_list[i].h      = window_list[i].h;
        context->animation_list[i].cid    = context->animation_connection;
        context->animation_list[i].skip   = false;
        memset(&context->animation_list[i].proxy, 0, sizeof(struct window_proxy));

        struct window_animation *existing_animation = table_find(&g_window_manager.window_animations_table, &context->animation_list[i].wid);
        if (existing_animation) {
            __atomic_store_n(&existing_animation->skip, true, __ATOMIC_RELEASE);

            context->animation_list[i].proxy.frame.origin.x    = (int)(existing_animation->proxy.tx);
            context->animation_list[i].proxy.frame.origin.y    = (int)(existing_animation->proxy.ty);
            context->animation_list[i].proxy.frame.size.width  = (int)(existing_animation->proxy.tw);
            context->animation_list[i].proxy.frame.size.height = (int)(existing_animation->proxy.th);
            context->animation_list[i].proxy.tx                = (int)(existing_animation->proxy.tx);
            context->animation_list[i].proxy.ty                = (int)(existing_animation->proxy.ty);
            context->animation_list[i].proxy.tw                = (int)(existing_animation->proxy.tw);
            context->animation_list[i].proxy.th                = (int)(existing_animation->proxy.th);
            context->animation_list[i].proxy.level             = existing_animation->proxy.level;
            context->animation_list[i].proxy.sub_level         = existing_animation->proxy.sub_level;
            context->animation_list[i].proxy.image             = existing_animation->proxy.image
                                                               ? (CGImageRef) CFRetain(existing_animation->proxy.image)
                                                               : NULL;
            __asm__ __volatile__ ("" ::: "memory");

            float alpha = 1.0f;
            SLSGetWindowAlpha(context->animation_connection, context->animation_list[i].wid, &alpha);
            window_manager_create_window_proxy(context->animation_connection, alpha, &context->animation_list[i].proxy);
            window_manager_notify_jankyborders(&context->animation_list[i], 1, 1325, true, false);
            window_manager_notify_jankyborders(existing_animation, 1, 1326, false, false);

            CFTypeRef transaction = SLSTransactionCreate(context->animation_connection);
            SLSTransactionOrderWindowGroup(transaction, context->animation_list[i].proxy.id, 1, context->animation_list[i].wid);
            SLSTransactionSetWindowSystemAlpha(transaction, existing_animation->proxy.id, 0);
            SLSTransactionCommit(transaction, 0);
            CFRelease(transaction);

            table_remove(&g_window_manager.window_animations_table, &context->animation_list[i].wid);
            window_manager_destroy_window_proxy(existing_animation->cid, &existing_animation->proxy);
        } else {
            pthread_t thread;
            if (pthread_create(&thread, NULL, &window_manager_build_window_proxy_thread_proc, &context->animation_list[i]) == 0) {
                threads[thread_count++] = thread;
            } else {
                window_manager_build_window_proxy_thread_proc(&context->animation_list[i]);
            }
        }

        table_add(&g_window_manager.window_animations_table, &context->animation_list[i].wid, &context->animation_list[i]);
    }
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);
    });

    TIME_BODY(window_manager_animate_window_list_async___wait_for_threads, {
    for (int i = 0; i < thread_count; ++i) {
        pthread_join(threads[i], NULL);
    }
    });

    TIME_BODY(window_manager_animate_window_list_async___swap_proxy_in, {
    scripting_addition_swap_window_proxy_in(context->animation_list, context->animation_count);
    });

    TIME_BODY(window_manager_animate_window_list_async___notify_jb, {
    window_manager_notify_jankyborders(context->animation_list, context->animation_count, 1325, true, false);
    });

    TIME_BODY(window_manager_animate_window_list_async___set_frame, {
    for (int i = 0; i < window_count; ++i) {
        window_manager_set_window_frame(context->animation_list[i].window, context->animation_list[i].x, context->animation_list[i].y, context->animation_list[i].w, context->animation_list[i].h);
    }
    });

    CVDisplayLinkRef link;
    SLSReenableUpdate(context->animation_connection);
    CVDisplayLinkCreateWithActiveCGDisplays(&link);
    CVDisplayLinkSetOutputCallback(link, window_manager_animate_window_list_thread_proc, context);
    CVDisplayLinkStart(link);
}

void window_manager_animate_window_list(struct window_capture *window_list, int window_count)
{
    TIME_FUNCTION;

    if (g_window_manager.window_animation_duration) {
        window_manager_animate_window_list_async(window_list, window_count);
    } else {
        for (int i = 0; i < window_count; ++i) {
            window_manager_set_window_frame(window_list[i].window, window_list[i].x, window_list[i].y, window_list[i].w, window_list[i].h);
        }
    }
}

void window_manager_animate_window(struct window_capture capture)
{
    TIME_FUNCTION;

    if (g_window_manager.window_animation_duration) {
        window_manager_animate_window_list_async(&capture, 1);
    } else {
        window_manager_set_window_frame(capture.window, capture.x, capture.y, capture.w, capture.h);
    }
}

void window_manager_set_window_frame(struct window *window, float x, float y, float width, float height)
{
    //
    // NOTE(asmvik): Attempting to check the window frame cache to prevent unnecessary movement and resize calls to the AX API
    // is not reliable because it is possible to perform operations that should be applied, at a higher rate than the AX API events
    // are received, causing our cache to become out of date and incorrectly guard against some changes that **should** be applied.
    // This causes the window layout to **not** be modified the way we expect.
    //
    // A possible solution is to use the faster CG window notifications, as they are **a lot** more responsive, and can be used to
    // track changes to the window frame in real-time without delay.
    //

    AX_ENHANCED_UI_WORKAROUND(window->application->ref, {
        CGPoint position = CGPointMake(x, y);
        CFTypeRef position_ref = AXValueCreate(kAXValueTypeCGPoint, (void *) &position);

        CGSize size = CGSizeMake(width, height);
        CFTypeRef size_ref = AXValueCreate(kAXValueTypeCGSize, (void *) &size);

        // NOTE(asmvik): Due to macOS constraints (visible screen-area), we might need to resize the window *before* moving it.
        if (size_ref) AXUIElementSetAttributeValue(window->ref, kAXSizeAttribute, size_ref);

        if (position_ref) {
            AXUIElementSetAttributeValue(window->ref, kAXPositionAttribute, position_ref);
            CFRelease(position_ref);
        }

        // NOTE(asmvik): Due to macOS constraints (visible screen-area), we might need to resize the window *after* moving it.
        if (size_ref) {
            AXUIElementSetAttributeValue(window->ref, kAXSizeAttribute, size_ref);
            CFRelease(size_ref);
        }
    });
}
