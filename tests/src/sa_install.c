// Exercise root-install helpers only on throwaway files; never install or
// restart Dock from the unit suite.
TEST_FUNC(sa_install_inputs,
{
    uid_t uid = 0;
    TEST_CHECK(scripting_addition_parse_sudo_uid("501", &uid), true);
    TEST_CHECK(uid, 501);
    TEST_CHECK(scripting_addition_parse_sudo_uid("501junk", &uid), false);
    TEST_CHECK(scripting_addition_parse_sudo_uid(" 501", &uid), false);
    TEST_CHECK(scripting_addition_parse_sudo_uid("-1", &uid), false);
    TEST_CHECK(scripting_addition_parse_sudo_uid("4294967296", &uid), false);

    // The login user comes from sudo, or, when root runs --load-sa directly
    // as nix-darwin's boot daemon does, from the owner of the console; never
    // root, whom loginwindow's console belongs to before anyone logs in.
    char *saved_sudo_uid = getenv("SUDO_UID") ? strdup(getenv("SUDO_UID")) : NULL;
    setenv("SUDO_UID", "501", 1);
    TEST_CHECK(scripting_addition_login_uid(&uid) && uid == 501, true);
    setenv("SUDO_UID", "0", 1);
    TEST_CHECK(scripting_addition_login_uid(&uid), false);
    setenv("SUDO_UID", "501junk", 1);
    TEST_CHECK(scripting_addition_login_uid(&uid), false);
    unsetenv("SUDO_UID");
    struct stat console;
    bool console_user = stat("/dev/console", &console) == 0 && console.st_uid != 0;
    bool found = scripting_addition_login_uid(&uid);
    TEST_CHECK(found, console_user);
    if (found) TEST_CHECK(uid, console.st_uid);
    if (saved_sudo_uid) setenv("SUDO_UID", saved_sudo_uid, 1);
    free(saved_sudo_uid);

    char loader_path[] = "/tmp/yabai-sa-loader-XXXXXX";
    char payload_path[] = "/tmp/yabai-sa-payload-XXXXXX";
    int loader = mkstemp(loader_path);
    int payload = mkstemp(payload_path);
    if (loader == -1 || payload == -1) {
        if (loader != -1) { close(loader); unlink(loader_path); }
        if (payload != -1) { close(payload); unlink(payload_path); }
        return false;
    }
    close(loader);
    close(payload);

    char saved_loader[MAXLEN], saved_payload[MAXLEN];
    memcpy(saved_loader, osax_bin_loader, sizeof(saved_loader));
    memcpy(saved_payload, osax_bin_payload, sizeof(saved_payload));
    snprintf(osax_bin_loader, sizeof(osax_bin_loader), "%s", loader_path);
    snprintf(osax_bin_payload, sizeof(osax_bin_payload), "%s", payload_path);

    // A failing signer must prevent the installer from claiming success.
    TEST_CHECK(scripting_addition_prepare_binaries("/usr/bin/false"), false);

    memcpy(osax_bin_loader, saved_loader, sizeof(saved_loader));
    memcpy(osax_bin_payload, saved_payload, sizeof(saved_payload));
    unlink(loader_path);
    unlink(payload_path);
});
