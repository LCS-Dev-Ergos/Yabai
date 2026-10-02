#ifndef YABAI_SOCKET_DEADLINE_H
#define YABAI_SOCKET_DEADLINE_H

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>

static inline bool yabai_socket_deadline_after_seconds(unsigned seconds, uint64_t *deadline)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;
    *deadline = (uint64_t) (now.tv_sec + seconds) * 1000000000ULL + now.tv_nsec;
    return true;
}

// POLLIN can accompany EOF; recv returns zero in that case. MSG_DONTWAIT
// closes the race between poll and recv without changing the socket's flags.
static inline ssize_t yabai_socket_recv_before_deadline(int fd, void *buffer, size_t length, uint64_t deadline)
{
    for (;;) {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
        uint64_t current = (uint64_t) now.tv_sec * 1000000000ULL + now.tv_nsec;
        if (current >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }

        uint64_t remaining_ms = (deadline - current + 999999ULL) / 1000000ULL;
        int timeout = remaining_ms > INT_MAX ? INT_MAX : (int) remaining_ms;
        struct pollfd peer = { .fd = fd, .events = POLLIN };
        int ready = poll(&peer, 1, timeout);
        if (ready == -1 && errno == EINTR) continue;
        if (ready == -1) return -1;
        if (ready == 0) continue;

        ssize_t count = recv(fd, buffer, length, MSG_DONTWAIT);
        if (count == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        return count;
    }
}

static inline bool yabai_socket_read_exact_before_deadline(int fd, void *buffer, size_t length, uint64_t deadline)
{
    size_t received = 0;
    while (received < length) {
        ssize_t count = yabai_socket_recv_before_deadline(fd, (char *) buffer + received,
                                                          length - received, deadline);
        if (count <= 0) return false;
        received += (size_t) count;
    }
    return true;
}

#endif
