// The daemon's peer check and its cache of trusted code, see
// src/osax/socket_identity.h. Real processes connect to a socket of ours: nc
// stands for a client that satisfies the requirement `anchor apple`, and this
// test, which does not, for one that fails it.
#include <assert.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>

#define YABAI_ALLOW_UNSIGNED_LOCAL 0
#include "../../src/osax/socket_identity.h"

extern char **environ;

static char socket_path[sizeof(((struct sockaddr_un *) 0)->sun_path)];
static int listener;

struct peer
{
    pid_t pid;
    int input;
    int sockfd;
};

// nc connected to our socket. Its standard input stays open, so it stays
// connected until the peer is closed.
static struct peer peer_connect_nc(void)
{
    struct peer peer = {0};
    int input[2];
    assert(pipe(input) == 0);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&actions, input[1]);

    char *argv[] = { "/usr/bin/nc", "-U", socket_path, NULL };
    assert(posix_spawn(&peer.pid, argv[0], &actions, NULL, argv, environ) == 0);
    posix_spawn_file_actions_destroy(&actions);

    close(input[0]);
    peer.input = input[1];
    peer.sockfd = accept(listener, NULL, NULL);
    assert(peer.sockfd != -1);
    return peer;
}

// This process connected to our socket.
static struct peer peer_connect_self(void)
{
    struct peer peer = {0};
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path);

    peer.input = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(connect(peer.input, (struct sockaddr *) &address, sizeof(address)) == 0);
    peer.sockfd = accept(listener, NULL, NULL);
    assert(peer.sockfd != -1);
    return peer;
}

static void peer_close(struct peer peer)
{
    close(peer.sockfd);
    close(peer.input);
    if (peer.pid) {
        kill(peer.pid, SIGTERM);
        waitpid(peer.pid, NULL, 0);
    }
}

static SecRequirementRef requirement_create(const char *text)
{
    SecRequirementRef requirement = NULL;
    CFStringRef string = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    assert(SecRequirementCreateWithString(string, kSecCSDefaultFlags, &requirement) == errSecSuccess);
    CFRelease(string);
    return requirement;
}

static double milliseconds(void)
{
    return (double) clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1e6;
}

int main(void)
{
    char directory[] = "/tmp/yabai-peer-XXXXXX";
    assert(mkdtemp(directory));
    snprintf(socket_path, sizeof(socket_path), "%s/peer.sock", directory);

    struct sockaddr_un address = { .sun_family = AF_UNIX };
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path);
    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(bind(listener, (struct sockaddr *) &address, sizeof(address)) == 0);
    assert(listen(listener, 8) == 0);

    SecRequirementRef apple = requirement_create("anchor apple");
    SecRequirementRef nothing = requirement_create("identifier \"com.example.nothing\"");
    struct yabai_socket_trusted_code trusted = {0};

    // The first nc is checked in full and its code remembered.
    struct peer peer = peer_connect_nc();
    double start = milliseconds();
    assert(yabai_socket_peer_is_trusted_cached(peer.sockfd, getuid(), apple, &trusted));
    double full = milliseconds() - start;
    assert(trusted.checks == 1 && trusted.count == 1);
    peer_close(peer);

    // Another nc is a new process with the same code: no second check. A peer
    // of another user is refused all the same.
    peer = peer_connect_nc();
    start = milliseconds();
    assert(yabai_socket_peer_is_trusted_cached(peer.sockfd, getuid(), apple, &trusted));
    double cached = milliseconds() - start;
    assert(trusted.checks == 1 && trusted.count == 1);
    assert(!yabai_socket_peer_is_trusted_cached(peer.sockfd, getuid() + 1, apple, &trusted));
    assert(trusted.checks == 1);

    // The cache serves one requirement: under another it starts over, and nc
    // fails that one.
    assert(!yabai_socket_peer_is_trusted_cached(peer.sockfd, getuid(), nothing, &trusted));
    assert(trusted.requirement == nothing && trusted.checks == 1 && trusted.count == 0);
    peer_close(peer);

    // Code that fails the requirement is checked every time, never remembered.
    for (int i = 0; i < 2; ++i) {
        peer = peer_connect_self();
        assert(!yabai_socket_peer_is_trusted_cached(peer.sockfd, getuid(), apple, &trusted));
        peer_close(peer);
    }
    assert(trusted.requirement == apple && trusted.checks == 2 && trusted.count == 0);

    close(listener);
    unlink(socket_path);
    rmdir(directory);
    CFRelease(apple);
    CFRelease(nothing);

    printf("peer check: full %.3f ms, cached %.3f ms\n", full, cached);
    return 0;
}
