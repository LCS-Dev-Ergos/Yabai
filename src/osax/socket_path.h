#ifndef YABAI_SOCKET_PATH_H
#define YABAI_SOCKET_PATH_H

// Shared private socket location for daemon and Dock.
// The directory is owned by the login user and inaccessible to other users.
#include <errno.h>
#include <pwd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

enum yabai_socket_name {
    YABAI_SOCKET_DAEMON,
    YABAI_SOCKET_PAYLOAD,
    YABAI_SOCKET_LOCK
};

static inline bool yabai_socket_path(uid_t owner, enum yabai_socket_name name,
                                     char *path, size_t capacity, bool create_directory)
{
    static const char *names[] = { "daemon.sock", "payload.sock", "daemon.lock" };
    if (name < YABAI_SOCKET_DAEMON || name > YABAI_SOCKET_LOCK) return false;

    struct passwd *account = getpwuid(owner);
    if (!account || !account->pw_dir) return false;

    char directory[sizeof(((struct sockaddr_un *) 0)->sun_path)];
    int length = snprintf(directory, sizeof(directory), "%s/Library/Caches/yabai", account->pw_dir);
    if (length < 0 || (size_t) length >= sizeof(directory)) return false;

    if (create_directory && geteuid() == owner && mkdir(directory, 0700) == -1 && errno != EEXIST) return false;

    struct stat info;
    if (lstat(directory, &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != owner || (info.st_mode & 077) != 0) return false;

    length = snprintf(path, capacity, "%s/%s", directory, names[name]);
    return length >= 0 && (size_t) length < capacity &&
           (size_t) length < sizeof(((struct sockaddr_un *) 0)->sun_path);
}

static inline bool yabai_socket_remove_stale(const char *path, uid_t owner)
{
    struct stat info;
    if (lstat(path, &info) == -1) return errno == ENOENT;
    return S_ISSOCK(info.st_mode) && info.st_uid == owner && unlink(path) == 0;
}

#endif
