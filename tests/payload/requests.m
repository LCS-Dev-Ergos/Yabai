//
// The payload's request handling outside Dock: replies to a daemon that has
// gone. payload.m is compiled in with its constructor disabled and SkyLight
// stubbed (skylight.h).
//

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/CADisplayLink.h>
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#include <assert.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <math.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

// Keeps load_payload from starting the socket daemon when the test loads.
#define constructor unused
#include "payload.m"
#undef constructor

#include "skylight.h"

static char request[SA_SOCKET_BUFF_LEN];
static char *request_end;

#define request_begin(op) (request_end = request, *request_end++ = (char) (op))
#define request_pack(v) (memcpy(request_end, &(v), sizeof(v)), request_end += sizeof(v))

// Sends the request framed as the daemon does.
static void request_send(int sockfd)
{
    int16_t length = (int16_t) (request_end - request);
    assert(write(sockfd, &length, sizeof(length)) == sizeof(length));
    assert(write(sockfd, request, length) == length);
}

static volatile sig_atomic_t broken_pipes;

static void count_broken_pipe(int signal)
{
    ++broken_pipes;
}

// The daemon stops waiting after a second and closes its end; a reply that
// comes later must not raise SIGPIPE, which Dock neither ignores nor catches.
// A request whose sender has already gone is not read at all (see
// test_stalled_request).
static void test_reply_to_closed_daemon(void)
{
    struct sigaction action = { .sa_handler = count_broken_pipe };
    struct sigaction saved;
    sigaction(SIGPIPE, &action, &saved);

    for (int i = 0; i < 2; ++i) {
        if (i == 0) {
            request_begin(SA_OPCODE_HANDSHAKE);
        } else {
            uint32_t display = 1, count = 1, wid = 5;
            uint8_t phase = SA_OPACITY_RESTORE;
            float alpha = 1.0f, duration = 0.0f, interval = 1.0f / 60.0f;

            request_begin(SA_OPCODE_WINDOW_OPACITY_BATCH);
            request_pack(display);
            request_pack(phase);
            request_pack(alpha);
            request_pack(duration);
            request_pack(interval);
            request_pack(count);
            request_pack(wid);
            request_pack(alpha);
        }

        int fds[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        request_send(fds[0]);

        // The daemon stops waiting while the handler runs.
        char message[SA_SOCKET_BUFF_LEN];
        assert(read_message(fds[1], message));
        close(fds[0]);
        @autoreleasepool {
            handle_message(fds[1], message);
        }
        close(fds[1]);
    }

    sigaction(SIGPIPE, &saved, NULL);
    assert(broken_pipes == 0);
}

int main(void)
{
    test_reply_to_closed_daemon();

    puts("payload: replies passed");
    return 0;
}
