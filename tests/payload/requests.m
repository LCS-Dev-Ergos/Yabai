//
// The payload's request handling outside Dock: replies to a daemon that has
// gone, the bound on reading a request and the pattern search's bound.
// payload.m is compiled in with its constructor disabled and SkyLight stubbed
// (skylight.h).
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

static double seconds(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec + time.tv_nsec / 1e9;
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

// A daemon that connects and stops sending, stopped in a debugger for
// instance, gives up the payload's only thread after the read deadline.
static void test_stalled_request(void)
{
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    assert(payload_connection_admit(fds[1]));

    int16_t length = 8;
    assert(write(fds[0], &length, sizeof(length)) == sizeof(length));
    assert(write(fds[0], "ab", 2) == 2);

    char message[SA_SOCKET_BUFF_LEN];
    double start = seconds();
    assert(!read_message(fds[1], message));
    double elapsed = seconds() - start;
    assert(elapsed >= 0.9 && elapsed < 3.0);

    close(fds[0]);
    close(fds[1]);

    // Once the sender has gone the request is not read, so a request the
    // daemon gave up on is never applied late.
    request_begin(SA_OPCODE_HANDSHAKE);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    request_send(fds[0]);
    close(fds[0]);
    assert(!read_message(fds[1], message));
    close(fds[1]);
}

struct trickle
{
    int sockfd;
    int count;
    useconds_t interval;
};

// Sends a request of `count` bytes one byte at a time.
static void *trickle_send(void *context)
{
    struct trickle *trickle = context;
    int16_t length = (int16_t) trickle->count;
    send(trickle->sockfd, &length, sizeof(length), MSG_NOSIGNAL);

    for (int i = 0; i < trickle->count; ++i) {
        usleep(trickle->interval);
        if (send(trickle->sockfd, "a", 1, MSG_NOSIGNAL) != 1) break;
    }

    return NULL;
}

// The timeout bounds the whole request, not each read: a byte every 300 ms
// would satisfy every read and hold the thread for 2.4 s.
static void test_trickled_request(void)
{
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    struct trickle trickle = { .sockfd = fds[0], .count = 8, .interval = 300000 };
    pthread_t sender;
    assert(pthread_create(&sender, NULL, trickle_send, &trickle) == 0);

    char message[SA_SOCKET_BUFF_LEN];
    double start = seconds();
    bool read = read_message(fds[1], message);
    double elapsed = seconds() - start;
    close(fds[1]);
    pthread_join(sender, NULL);
    close(fds[0]);

    assert(!read);
    assert(elapsed >= 0.9 && elapsed < 1.6);
}

// A lookup matches only within the bytes it may read: a match that would run
// past the end of Dock's code is no match, and no byte past it is read.
static void test_pattern_search_bounds(void)
{
    size_t page = (size_t) sysconf(_SC_PAGESIZE);
    uint8_t *pages = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    assert(pages != MAP_FAILED);
    assert(mprotect(pages + page, page, PROT_NONE) == 0);

    uint8_t *end = pages + page;
    end[-3] = 0xAA;
    end[-2] = 0xBB;
    end[-1] = 0xCC;

    uint64_t start = (uint64_t) pages;
    assert(hex_find_seq(start, (uint64_t) end, "AA BB CC DD") == 0);
    assert(hex_find_seq(start, (uint64_t) end, "AA BB CC") == (uint64_t) (end - 3));
    assert(hex_find_seq(start, (uint64_t) end, "?? BB CC") == (uint64_t) (end - 3));
    assert(hex_find_seq((uint64_t) end, (uint64_t) end, "AA") == 0);
    assert(hex_find_seq(start, start + 2, "00 00 00") == 0);

    munmap(pages, 2 * page);
}

int main(void)
{
    test_reply_to_closed_daemon();
    test_stalled_request();
    test_trickled_request();
    test_pattern_search_bounds();

    puts("payload: replies, read deadline and pattern bounds passed");
    return 0;
}
