//
// libFuzzer target for the scripting-addition request handling that runs
// inside Dock.app: framing (read_message) and every handler's parsing.
//
// payload.m is compiled in with its constructor disabled, and the SkyLight
// calls it makes are stubbed (../payload/skylight.h), so no request reaches
// WindowServer. The space handlers return before parsing because no Dock
// instances are resolved.
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

#include "../payload/skylight.h"

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
