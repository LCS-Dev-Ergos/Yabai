#include <crt_externs.h>

static int event_signal_spawn(struct event_signal *es, char *command)
{
    posix_spawnattr_t attributes;
    int status = posix_spawnattr_init(&attributes);
    if (status) return status;

    posix_spawn_file_actions_t actions;
    status = posix_spawn_file_actions_init(&actions);
    if (status) {
        posix_spawnattr_destroy(&attributes);
        return status;
    }

    // A signal must never keep queued clients or daemon resources open.
    status = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
    for (int fd = STDIN_FILENO; !status && fd <= STDERR_FILENO; ++fd) {
        if (fcntl(fd, F_GETFD) != -1) {
            status = posix_spawn_file_actions_addinherit_np(&actions, fd);
        }
    }

    // Pass overrides to env without changing the daemon's environment.
    char *variables[4] = {0};
    char *exec[9] = { "/usr/bin/env" };
    int argc = 1;

    for (int i = 0; !status && i < 4; ++i) {
        if (!es->arg_name[i]) continue;

        if (asprintf(&variables[i], "%s=%s", es->arg_name[i], es->arg_value[i]) == -1) {
            status = ENOMEM;
            break;
        }

        exec[argc++] = variables[i];
    }

    if (!status) {
        exec[argc++] = "sh";
        exec[argc++] = "-c";
        exec[argc++] = command;
        exec[argc] = NULL;

        status = posix_spawn(NULL, exec[0], &actions, &attributes, exec, *_NSGetEnviron());
    }

    for (int i = 0; i < 4; ++i) free(variables[i]);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    return status;
}
