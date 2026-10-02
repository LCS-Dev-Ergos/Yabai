//
// The payload's request handling outside Dock: replies to a daemon that has
// gone, the bound on reading a request, the pattern search's bound and the
// window scale handler. payload.m is compiled in with its constructor
// disabled and SkyLight stubbed (skylight.h).
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

// Hands the request to the handlers as read_message would.
static void request_handle(void)
{
    message_end = request_end;
    @autoreleasepool {
        handle_message(-1, request);
    }
}

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
        close(fds[0]);

        char message[SA_SOCKET_BUFF_LEN];
        assert(read_message(fds[1], message));
        @autoreleasepool {
            handle_message(fds[1], message);
        }
        close(fds[1]);
    }

    sigaction(SIGPIPE, &saved, NULL);
    assert(broken_pipes == 0);
}

// A daemon that connects and stops sending, stopped in a debugger for
// instance, gives up the payload's only thread after the read timeout.
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
}

struct request_writer { int sockfd; bool trickle; };

static void *write_request_fragments(void *context)
{
    struct request_writer *writer = context;
    int16_t length = writer->trickle ? 4 : 1;
    if (writer->trickle) {
        write(writer->sockfd, &length, sizeof(length));
        for (int i = 0; i < length; ++i) {
            char byte = 'a';
            write(writer->sockfd, &byte, 1);
            usleep(600000);
        }
    } else {
        write(writer->sockfd, &length, 1);
        usleep(50000);
        write(writer->sockfd, (char *) &length + 1, 1);
        char opcode = SA_OPCODE_HANDSHAKE;
        write(writer->sockfd, &opcode, 1);
    }
    close(writer->sockfd);
    return NULL;
}

static void test_request_deadline_and_fragmented_header(void)
{
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    struct request_writer writer = { fds[0], false };
    pthread_t server;
    pthread_create(&server, NULL, write_request_fragments, &writer);
    char message[SA_SOCKET_BUFF_LEN];
    assert(read_message(fds[1], message));
    assert(message[0] == SA_OPCODE_HANDSHAKE);
    pthread_join(server, NULL);
    close(fds[1]);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    writer = (struct request_writer) { fds[0], true };
    pthread_create(&server, NULL, write_request_fragments, &writer);
    double start = seconds();
    assert(!read_message(fds[1], message));
    double elapsed = seconds() - start;
    pthread_join(server, NULL);
    close(fds[1]);
    assert(elapsed >= 0.9 && elapsed < 1.7);
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

static void request_window_scale(uint32_t wid, float x, float y, float w, float h)
{
    request_begin(SA_OPCODE_WINDOW_SCALE);
    request_pack(wid);
    request_pack(x);
    request_pack(y);
    request_pack(w);
    request_pack(h);
    request_handle();
}

// The scale handler shrinks a window to a quarter of the display width and
// never hands WindowServer a transform that is not finite.
static void test_window_scale(void)
{
    stub_window_transform_sets = 0;
    request_window_scale(1, 0.0f, 0.0f, 1600.0f, 900.0f);
    assert(stub_window_transform_sets == 1);
    assert(isfinite(stub_window_transform_set.a) && isfinite(stub_window_transform_set.d));

    // At 400 points wide, this window would be no point high.
    stub_window_bounds = CGRectMake(0, 0, 8000, 10);
    stub_window_transform_sets = 0;
    request_window_scale(1, 0.0f, 0.0f, 1600.0f, 900.0f);
    assert(stub_window_transform_sets == 0);

    // Without the current transform we cannot tell scaling from restoring.
    stub_window_bounds = CGRectMake(0, 0, 800, 600);
    stub_window_transform_error = kCGErrorFailure;
    request_window_scale(1, 0.0f, 0.0f, 1600.0f, 900.0f);
    assert(stub_window_transform_sets == 0);
    stub_window_transform_error = 0;

    stub_window_bounds = CGRectMake(0, 0, 800, INFINITY);
    request_window_scale(1, 0.0f, 0.0f, 1600.0f, 900.0f);
    assert(stub_window_transform_sets == 0);

    stub_window_bounds = CGRectMake(0, 0, 1e-300, 1e300);
    request_window_scale(1, 0.0f, 0.0f, 1600.0f, 900.0f);
    assert(stub_window_transform_sets == 0);
}

static void test_proxy_swap_atomicity(void)
{
    int count = 2;
    uint32_t wid = 10, proxy = 20;
    stub_transaction_creates = 0;
    stub_transaction_commits = 0;
    stub_transaction_alpha_sets = 0;
    request_begin(SA_OPCODE_WINDOW_SWAP_PROXY_IN);
    request_pack(count);
    request_pack(wid);
    request_pack(proxy);
    request_handle();
    assert(stub_transaction_creates == 0 && stub_transaction_alpha_sets == 0);

    request_begin(SA_OPCODE_WINDOW_SWAP_PROXY_OUT);
    request_pack(count);
    request_pack(wid);
    request_pack(proxy);
    request_handle();
    assert(stub_transaction_commits == 0);

    // The sender's zero sentinel occupies one word and remains valid.
    request_begin(SA_OPCODE_WINDOW_SWAP_PROXY_IN);
    request_pack(count);
    request_pack(wid);
    request_pack(proxy);
    wid = 0;
    request_pack(wid);
    request_handle();
    assert(stub_transaction_commits == 1 && stub_transaction_alpha_sets == 1);
}

int main(void)
{
    test_reply_to_closed_daemon();
    test_stalled_request();
    test_request_deadline_and_fragmented_header();
    test_pattern_search_bounds();
    test_window_scale();
    test_proxy_swap_atomicity();

    puts("payload: replies, read timeout, pattern bounds, scale and proxy swap passed");
    return 0;
}
