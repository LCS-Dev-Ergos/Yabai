static TEST_SIG(signal_environment)
{
    char *test_name = "signal_environment";
    bool result = true;
    char path[] = "/tmp/yabai-signal-env-XXXXXX";
    int output = mkstemp(path);
    if (output == -1) return false;

    char *previous = getenv("YABAI_SIGNAL_TEST");
    char *saved = previous ? strdup(previous) : NULL;
    setenv("YABAI_SIGNAL_TEST", "parent", 1);

    struct event_signal event = {
        .arg_name = { "YABAI_SIGNAL_TEST", "YABAI_SIGNAL_ARG2",
                      "YABAI_SIGNAL_ARG3", "YABAI_SIGNAL_ARG4" },
        .arg_value = { "child one", "quote'\"$;", "", "last" }
    };

    char command[512];
    snprintf(command, sizeof(command),
             "printf '%%s|%%s|%%s|%%s|%%s\\n' \"$YABAI_SIGNAL_TEST\" "
             "\"$YABAI_SIGNAL_ARG2\" \"$YABAI_SIGNAL_ARG3\" "
             "\"$YABAI_SIGNAL_ARG4\" \"${PATH:+path}\" >> '%s'", path);

    TEST_CHECK(event_signal_spawn(&event, command), 0);
    event.arg_value[0] = "child two";
    TEST_CHECK(event_signal_spawn(&event, command), 0);
    TEST_CHECK(strcmp(getenv("YABAI_SIGNAL_TEST"), "parent"), 0);

    char contents[256] = {0};
    const char *first = "child one|quote'\"$;||last|path\n";
    const char *second = "child two|quote'\"$;||last|path\n";
    size_t expected = strlen(first) + strlen(second);

    for (int i = 0; i < 200; ++i) {
        if (pread(output, contents, sizeof(contents)-1, 0) == expected) break;
        usleep(5000);
    }

    TEST_CHECK(strstr(contents, first) != NULL, true);
    TEST_CHECK(strstr(contents, second) != NULL, true);
    TEST_CHECK(strlen(contents) == expected, true);

    if (saved) {
        setenv("YABAI_SIGNAL_TEST", saved, 1);
        free(saved);
    } else {
        unsetenv("YABAI_SIGNAL_TEST");
    }

    close(output);
    unlink(path);
    return result;
}

static TEST_SIG(signal_standard_output)
{
    char *test_name = "signal_standard_output";
    bool result = true;
    char path[] = "/tmp/yabai-signal-stdio-XXXXXX";
    int output = mkstemp(path);
    if (output == -1) return false;

    int saved_out = dup(STDOUT_FILENO);
    int saved_err = dup(STDERR_FILENO);
    if (saved_out == -1 || saved_err == -1) {
        if (saved_out != -1) close(saved_out);
        if (saved_err != -1) close(saved_err);
        close(output);
        unlink(path);
        return false;
    }

    fflush(stdout);
    fflush(stderr);
    dup2(output, STDOUT_FILENO);
    dup2(output, STDERR_FILENO);

    struct event_signal event = {0};
    int status = event_signal_spawn(&event, "printf out; printf err >&2");

    dup2(saved_out, STDOUT_FILENO);
    dup2(saved_err, STDERR_FILENO);
    close(saved_out);
    close(saved_err);
    TEST_CHECK(status, 0);

    char contents[7] = {0};
    for (int i = 0; i < 200 && pread(output, contents, 6, 0) != 6; ++i) {
        usleep(5000);
    }

    TEST_CHECK(strcmp(contents, "outerr"), 0);
    close(output);
    unlink(path);
    return result;
}
