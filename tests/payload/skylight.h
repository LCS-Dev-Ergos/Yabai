//
// SkyLight as payload.m sees it, for the targets that compile the payload
// outside Dock: no call reaches WindowServer. The window scale handler reads a
// window's bounds and transform; tests set what it reads and see what it sets.
//

static CGRect stub_window_bounds = { { 0, 0 }, { 800, 600 } };
static CGError stub_window_transform_error;
static int stub_window_transform_sets;
static CGAffineTransform stub_window_transform_set;

int SLSMainConnectionID(void) { return 0; }
CGError SLSGetConnectionPSN(int cid, ProcessSerialNumber *psn) { *psn = (ProcessSerialNumber) {0}; return 0; }
CGError SLSGetWindowAlpha(int cid, uint32_t wid, float *alpha) { *alpha = 1.0f; return 0; }
CGError SLSSetWindowAlpha(int cid, uint32_t wid, float alpha) { return 0; }
OSStatus SLSMoveWindowWithGroup(int cid, uint32_t wid, CGPoint *point) { return 0; }
CGError SLSReassociateWindowsSpacesByGeometry(int cid, CFArrayRef window_list) { return 0; }
CGError SLSGetWindowOwner(int cid, uint32_t wid, int *window_cid) { *window_cid = 0; return 0; }
CGError SLSSetWindowTags(int cid, uint32_t wid, uint64_t *tags, size_t tag_size) { return 0; }
CGError SLSClearWindowTags(int cid, uint32_t wid, uint64_t *tags, size_t tag_size) { return 0; }
CGError SLSGetWindowBounds(int cid, uint32_t wid, CGRect *frame) { *frame = stub_window_bounds; return 0; }
CGError SLSGetWindowTransform(int cid, uint32_t wid, CGAffineTransform *t)
{
    if (stub_window_transform_error) return stub_window_transform_error;
    *t = CGAffineTransformMakeTranslation(-stub_window_bounds.origin.x, -stub_window_bounds.origin.y);
    return 0;
}
CGError SLSSetWindowTransform(int cid, uint32_t wid, CGAffineTransform t)
{
    ++stub_window_transform_sets;
    stub_window_transform_set = t;
    return 0;
}
CGError SLSOrderWindow(int cid, uint32_t wid, int order, uint32_t rel_wid) { return 0; }
void SLSManagedDisplaySetCurrentSpace(int cid, CFStringRef display_ref, uint64_t sid) {}
uint64_t SLSManagedDisplayGetCurrentSpace(int cid, CFStringRef display_ref) { return 0; }
CFStringRef SLSCopyManagedDisplayForSpace(int cid, uint64_t sid) { return NULL; }
CFArrayRef SLSCopyManagedDisplaySpaces(int cid) { return NULL; }
CGError SLSMoveManagedSpaceToDisplayIndex(int cid, uint64_t sid, CFStringRef display_uuid, uint32_t index) { return 0; }
void SLSMoveWindowsToManagedSpace(int cid, CFArrayRef window_list, uint64_t sid) {}
void SLSShowSpaces(int cid, CFArrayRef space_list) {}
void SLSHideSpaces(int cid, CFArrayRef space_list) {}
CFTypeRef SLSTransactionCreate(int cid) { return CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks); }
CGError SLSTransactionCommit(CFTypeRef transaction, int synchronous) { return 0; }
CGError SLSTransactionOrderWindowGroup(CFTypeRef transaction, uint32_t wid, int order, uint32_t rel_wid) { return 0; }
CGError SLSTransactionSetWindowSystemAlpha(CFTypeRef transaction, uint32_t wid, float alpha) { return 0; }
CGError SLSTransactionSetSpaceAlpha(CFTypeRef transaction, uint64_t sid, float alpha) { return 0; }
CGError SLSTransactionSetSpaceAbsoluteLevel(CFTypeRef transaction, uint64_t sid, int level) { return 0; }
CGError SLSTransactionShowSpace(CFTypeRef transaction, uint64_t sid) { return 0; }
CGError SLSTransactionHideSpace(CFTypeRef transaction, uint64_t sid) { return 0; }
CGError SLSTransactionSetManagedDisplayCurrentSpace(CFTypeRef transaction, CFStringRef display, uint64_t sid) { return 0; }
CFArrayRef SLSCopyWindowsWithOptionsAndTags(int cid, uint32_t owner, CFArrayRef spaces, uint32_t options, uint64_t *set_tags, uint64_t *clear_tags) { return NULL; }
CGError SLSGetWindowLevel(int cid, uint32_t wid, int *level) { *level = 0; return 0; }
float SLSSpaceGetAlpha(int cid, uint64_t sid) { return 1.0f; }
int SLSSpaceGetAbsoluteLevel(int cid, uint64_t sid) { return 0; }
CGError SLSSetWindowSubLevel(int cid, uint32_t wid, int level) { return 0; }
