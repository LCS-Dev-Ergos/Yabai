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
