// The configuration file runs once, from its path as given: spaces and shell
// characters in that path are part of the name, not shell syntax.

static bool test_config_file_runs(char *test_name, bool executable)
{
    bool result = true;
    char directory[] = "/tmp/yabai-config dir;XXXXXX";
    if (!mkdtemp(directory)) return false;

    char config[256], marker[256];
    snprintf(config, sizeof(config), "%s/yabairc", directory);
    snprintf(marker, sizeof(marker), "%s/ran", directory);

    FILE *file = fopen(config, "w");
    if (!file) return false;
    fprintf(file, "#!/bin/sh\nprintf x >> \"$(dirname \"$0\")/ran\"\n");
    fclose(file);
    chmod(config, executable ? 0700 : 0600);

    exec_config_file(config, sizeof(config));

    struct stat info = {0};
    for (int i = 0; i < 400 && (stat(marker, &info) != 0 || info.st_size == 0); ++i) {
        usleep(5000);
    }
    usleep(50000);

    TEST_CHECK(stat(marker, &info), 0);
    TEST_CHECK((int) info.st_size, 1);

    unlink(marker);
    unlink(config);
    rmdir(directory);
    return result;
}

TEST_FUNC(config_file_path,
{
    bool executable = test_config_file_runs(test_name, true);
    TEST_CHECK(executable, true);

    bool script = test_config_file_runs(test_name, false);
    TEST_CHECK(script, true);
});
