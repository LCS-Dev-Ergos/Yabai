// Give Core Animation the captured image directly. Drawing a Retina Desktop
// through SLWindowContextCreate incurred a roughly 50 ms CPU copy on the test
// host. The remote layer/surface binding is also used by SketchyBar.
#ifndef SPACE_SNAPSHOT_CONTEXT_CLASS
#define SPACE_SNAPSHOT_CONTEXT_CLASS CAContext
@interface CAContext : NSObject
+ (instancetype)contextWithCGSConnection:(uint32_t)connection options:(NSDictionary *)options;
@property (nonatomic, retain) CALayer *layer;
@property (nonatomic, readonly) uint32_t contextId;
- (void)invalidate;
@end
#endif

extern CGError SLSAddSurface(int cid, uint32_t wid, uint32_t *surface);
extern CGError SLSRemoveSurface(int cid, uint32_t wid, uint32_t surface);
extern CGError SLSBindSurface(int cid, uint32_t wid, uint32_t surface, int mode, int flags, uint32_t context);
extern CGError SLSSetSurfaceBounds(int cid, uint32_t wid, uint32_t surface, CGRect bounds);
extern CGError SLSSetSurfaceOpacity(int cid, uint32_t wid, uint32_t surface, bool opaque);
extern CGError SLSOrderSurface(int cid, uint32_t wid, uint32_t surface, int mode, uint32_t relative);
extern CGError SLSSetSurfaceResolution(int cid, uint32_t wid, uint32_t surface, CGFloat scale);
extern CGError SLSSetSurfaceColorSpace(int cid, uint32_t wid, uint32_t surface, CGColorSpaceRef color_space);

static void space_snapshot_surface_destroy(struct space_snapshot *snapshot)
{
    if (snapshot->surface) SLSRemoveSurface(SLSMainConnectionID(), snapshot->window, snapshot->surface);
    SPACE_SNAPSHOT_CONTEXT_CLASS *context = snapshot->render_context;
    context.layer = nil;
    [snapshot->render_context invalidate];
    [snapshot->render_context release];
    [snapshot->layer release];
}

static bool space_snapshot_surface_create(struct space_snapshot *snapshot, CGImageRef image, CGRect bounds)
{
    int cid = SLSMainConnectionID();
    if (![SPACE_SNAPSHOT_CONTEXT_CLASS respondsToSelector:@selector(contextWithCGSConnection:options:)]) return false;
    bounds.origin = CGPointZero;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    snapshot->layer = [[CALayer alloc] init];
    snapshot->render_context = [[SPACE_SNAPSHOT_CONTEXT_CLASS contextWithCGSConnection:cid options:nil] retain];
    CALayer *layer = snapshot->layer;
    layer.anchorPoint = CGPointZero;
    layer.position = CGPointZero;
    layer.bounds = bounds;
    layer.contentsScale = CGImageGetWidth(image) / bounds.size.width;
    layer.opaque = YES;
    layer.contents = (id) image;
    SPACE_SNAPSHOT_CONTEXT_CLASS *context = snapshot->render_context;
    context.layer = layer;

    CGColorSpaceRef colors = CGImageGetColorSpace(image);
    bool success = layer && snapshot->render_context && colors
        && SLSAddSurface(cid, snapshot->window, &snapshot->surface) == kCGErrorSuccess
        && snapshot->surface
        && [snapshot->render_context contextId]
        && SLSBindSurface(cid, snapshot->window, snapshot->surface, 4, 0,
                          [snapshot->render_context contextId]) == kCGErrorSuccess
        && SLSSetSurfaceBounds(cid, snapshot->window, snapshot->surface, bounds) == kCGErrorSuccess
        && SLSSetSurfaceResolution(cid, snapshot->window, snapshot->surface, layer.contentsScale) == kCGErrorSuccess
        && SLSSetSurfaceOpacity(cid, snapshot->window, snapshot->surface, false) == kCGErrorSuccess
        && SLSSetSurfaceColorSpace(cid, snapshot->window, snapshot->surface, colors) == kCGErrorSuccess
        && SLSOrderSurface(cid, snapshot->window, snapshot->surface, 1, 0) == kCGErrorSuccess;
    [CATransaction commit];
    [CATransaction flush];
    // SLSFlushSurface returned 1000 even while the layer presented correctly
    // on the test host. CA's explicit commit/flush owns submission here.
    return success;
}
