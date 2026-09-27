//
// libFuzzer target for the scripting-addition request handling that runs
// inside Dock.app: framing (read_message) and every handler's parsing.
//
// payload.m is compiled in with its constructor disabled, and the SkyLight
// calls it makes are stubbed below, so no request reaches WindowServer. The
// space handlers return before parsing because no Dock instances are resolved.
// Opacity parsing runs with worker/main-queue scheduling disabled. The real
// scheduling, ownership and timing are covered by the dedicated fade tests.
//

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/CADisplayLink.h>
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

// Keeps load_payload from starting the socket daemon when the target loads.
#define constructor unused
#define dispatch_async(queue, block) ((void)0)
#include "payload.m"
#undef dispatch_async
#undef constructor

int SLSMainConnectionID(void) { return 0; }
CGError SLSGetConnectionPSN(int cid, ProcessSerialNumber *psn) { *psn = (ProcessSerialNumber) {0}; return 0; }
CGError SLSGetWindowAlpha(int cid, uint32_t wid, float *alpha) { *alpha = 1.0f; return 0; }
CGError SLSSetWindowAlpha(int cid, uint32_t wid, float alpha) { return 0; }
OSStatus SLSMoveWindowWithGroup(int cid, uint32_t wid, CGPoint *point) { return 0; }
CGError SLSReassociateWindowsSpacesByGeometry(int cid, CFArrayRef window_list) { return 0; }
CGError SLSGetWindowOwner(int cid, uint32_t wid, int *window_cid) { *window_cid = 0; return 0; }
CGError SLSSetWindowTags(int cid, uint32_t wid, uint64_t *tags, size_t tag_size) { return 0; }
CGError SLSClearWindowTags(int cid, uint32_t wid, uint64_t *tags, size_t tag_size) { return 0; }
CGError SLSGetWindowBounds(int cid, uint32_t wid, CGRect *frame) { *frame = CGRectMake(0, 0, 800, 600); return 0; }
CGError SLSGetWindowTransform(int cid, uint32_t wid, CGAffineTransform *t) { *t = CGAffineTransformIdentity; return 0; }
CGError SLSSetWindowTransform(int cid, uint32_t wid, CGAffineTransform t) { return 0; }
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
float SLSSpaceGetAlpha(int cid, uint64_t sid) { return 1.0f; }
int SLSSpaceGetAbsoluteLevel(int cid, uint64_t sid) { return 0; }
CGError SLSSetWindowSubLevel(int cid, uint32_t wid, int level) { return 0; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) abort();

    // A request larger than the socket buffer is truncated, as a slow client's would be.
    int buffer_size = 64 * 1024;
    setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
    setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    if (size) write(fds[0], data, size);
    shutdown(fds[0], SHUT_WR);

    char message[SA_SOCKET_BUFF_LEN];
    window_fade_worker_started = true; // No real thread can outlive one input.
    if (read_message(fds[1], message)) {
        @autoreleasepool {
            handle_message(fds[1], message);
        }
    }

    while (window_fades) window_fade_remove(&window_fades);

    close(fds[0]);
    close(fds[1]);
    return 0;
}
