#include "sa.h"
#include "../osax/socket_path.h"
#include "../osax/socket_identity.h"
#include "../osax/socket_deadline.h"
#include <errno.h>
#include <limits.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>

extern char **environ;


static char osax_base_dir[MAXLEN];
static char osax_contents_dir[MAXLEN];
static char osax_contents_macos_dir[MAXLEN];
static char osax_contents_res_dir[MAXLEN];
static char osax_info_plist[MAXLEN];
static char osax_payload_dir[MAXLEN];
static char osax_payload_contents_dir[MAXLEN];
static char osax_payload_contents_macos_dir[MAXLEN];
static char osax_payload_plist[MAXLEN];
static char osax_bin_payload[MAXLEN];
static char osax_bin_loader[MAXLEN];

static char sa_plist[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n"
    "<dict>\n"
    "<key>CFBundleDevelopmentRegion</key>\n"
    "<string>en</string>\n"
    "<key>CFBundleExecutable</key>\n"
    "<string>loader</string>\n"
    "<key>CFBundleIdentifier</key>\n"
    "<string>com.asmvik.yabai-osax</string>\n"
    "<key>CFBundleInfoDictionaryVersion</key>\n"
    "<string>6.0</string>\n"
    "<key>CFBundleName</key>\n"
    "<string>yabai</string>\n"
    "<key>CFBundlePackageType</key>\n"
    "<string>osax</string>\n"
    "<key>CFBundleShortVersionString</key>\n"
    "<string>"OSAX_VERSION"</string>\n"
    "<key>CFBundleVersion</key>\n"
    "<string>"OSAX_VERSION"</string>\n"
    "<key>NSHumanReadableCopyright</key>\n"
    "<string>Copyright © 2019 Åsmund Vikane. All rights reserved.</string>\n"
    "<key>OSAXHandlers</key>\n"
    "<dict>\n"
    "</dict>\n"
    "</dict>\n"
    "</plist>";

static char sa_bundle_plist[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n"
    "<dict>\n"
    "<key>CFBundleDevelopmentRegion</key>\n"
    "<string>en</string>\n"
    "<key>CFBundleExecutable</key>\n"
    "<string>payload</string>\n"
    "<key>CFBundleIdentifier</key>\n"
    "<string>com.asmvik.yabai-sa</string>\n"
    "<key>CFBundleInfoDictionaryVersion</key>\n"
    "<string>6.0</string>\n"
    "<key>CFBundleName</key>\n"
    "<string>payload</string>\n"
    "<key>CFBundlePackageType</key>\n"
    "<string>BNDL</string>\n"
    "<key>CFBundleShortVersionString</key>\n"
    "<string>"OSAX_VERSION"</string>\n"
    "<key>CFBundleVersion</key>\n"
    "<string>"OSAX_VERSION"</string>\n"
    "<key>NSHumanReadableCopyright</key>\n"
    "<string>Copyright © 2019 Åsmund Vikane. All rights reserved.</string>\n"
    "<key>NSPrincipalClass</key>\n"
    "<string></string>\n"
    "</dict>\n"
    "</plist>";

static void scripting_addition_set_path(void)
{
    snprintf(osax_base_dir, sizeof(osax_base_dir), "%s", "/Library/ScriptingAdditions/yabai.osax");

    snprintf(osax_contents_dir, sizeof(osax_contents_dir), "%s/%s", osax_base_dir, "Contents");
    snprintf(osax_contents_macos_dir, sizeof(osax_contents_macos_dir), "%s/%s", osax_contents_dir, "MacOS");
    snprintf(osax_contents_res_dir, sizeof(osax_contents_res_dir), "%s/%s", osax_contents_dir, "Resources");
    snprintf(osax_info_plist, sizeof(osax_info_plist), "%s/%s", osax_contents_dir, "Info.plist");

    snprintf(osax_payload_dir, sizeof(osax_payload_dir), "%s/%s", osax_contents_res_dir, "payload.bundle");
    snprintf(osax_payload_contents_dir, sizeof(osax_payload_contents_dir), "%s/%s", osax_payload_dir, "Contents");
    snprintf(osax_payload_contents_macos_dir, sizeof(osax_payload_contents_macos_dir), "%s/%s", osax_payload_contents_dir, "MacOS");
    snprintf(osax_payload_plist, sizeof(osax_payload_plist), "%s/%s", osax_payload_contents_dir, "Info.plist");

    snprintf(osax_bin_loader, sizeof(osax_bin_loader), "%s/%s", osax_contents_macos_dir, "loader");
    snprintf(osax_bin_payload, sizeof(osax_bin_payload), "%s/%s", osax_payload_contents_macos_dir, "payload");
}

static bool scripting_addition_create_directory(void)
{
    if (mkdir(osax_base_dir, 0755))                   goto err;
    if (mkdir(osax_contents_dir, 0755))               goto err;
    if (mkdir(osax_contents_macos_dir, 0755))         goto err;
    if (mkdir(osax_contents_res_dir, 0755))           goto err;
    if (mkdir(osax_payload_dir, 0755))                goto err;
    if (mkdir(osax_payload_contents_dir, 0755))       goto err;
    if (mkdir(osax_payload_contents_macos_dir, 0755)) goto err;
    return true;
err:
    return false;
}

static bool scripting_addition_write_file(char *buffer, unsigned int size, char *file, char *file_mode)
{
    FILE *handle = fopen(file, file_mode);
    if (!handle) return false;

    size_t bytes = fwrite(buffer, size, 1, handle);
    bool result = bytes == 1;
    if (fclose(handle) != 0) result = false;

    return result;
}

static bool scripting_addition_run_command(const char *path, char *const argv[])
{
    pid_t pid;
    if (posix_spawn(&pid, path, NULL, NULL, argv, environ) != 0) return false;

    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);
    return waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool scripting_addition_prepare_binaries(const char *codesign_path)
{
    if (chmod(osax_bin_loader, 0755) != 0 || chmod(osax_bin_payload, 0755) != 0) return false;

    char *loader_sign[] = { (char *) codesign_path, "-f", "-s", "-", osax_bin_loader, NULL };
    if (!scripting_addition_run_command(loader_sign[0], loader_sign)) return false;

    char *payload_sign[] = { (char *) codesign_path, "-f", "-s", "-", osax_bin_payload, NULL };
    return scripting_addition_run_command(payload_sign[0], payload_sign);
}

static void scripting_addition_restart_dock(void)
{
    NSArray *dock = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.dock"];
    [dock makeObjectsPerformSelector:@selector(terminate)];
}

static bool scripting_addition_parse_sudo_uid(const char *value, uid_t *uid)
{
    if (!value || !*value) return false;
    for (const char *cursor = value; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
    }
    errno = 0;
    char *end;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno != 0 || *end != '\0' || (uid_t) parsed != parsed) return false;
    *uid = (uid_t) parsed;
    return true;
}

//
// NOTE: The payload runs in the Dock of the user who logged in. sudo names that
// user in SUDO_UID; nix-darwin's boot daemon runs --load-sa as root without
// sudo, and launchd starts it again until it exits 0, so then the user is the
// one who owns the console. Before anyone logs in, loginwindow's console
// belongs to root: there is no user yet, and the daemon tries again later.
//
static bool scripting_addition_login_uid(uid_t *uid)
{
    const char *sudo_uid = getenv("SUDO_UID");
    if (sudo_uid) return scripting_addition_parse_sudo_uid(sudo_uid, uid) && *uid != 0;

    struct stat console;
    if (stat("/dev/console", &console) != 0 || console.st_uid == 0) return false;

    *uid = console.st_uid;
    return true;
}

static bool scripting_addition_set_socket_path(void)
{
    uid_t uid = getuid();
    assert(uid == 0);

    if (!scripting_addition_login_uid(&uid)) return false;

    return yabai_socket_path(uid, YABAI_SOCKET_PAYLOAD,
                             g_sa_socket_file, sizeof(g_sa_socket_file), false);
}

static bool scripting_addition_is_installed(void)
{
    if (osax_base_dir[0] == 0) scripting_addition_set_path();

    DIR *dir = opendir(osax_base_dir);
    if (!dir) return false;

    closedir(dir);
    return true;
}

static bool scripting_addition_path_is_safe(const char *path, bool directory, bool executable)
{
    struct stat info;
    if (lstat(path, &info) != 0 || info.st_uid != 0 || (info.st_mode & 022) != 0) return false;
    if (directory) return S_ISDIR(info.st_mode);
    return S_ISREG(info.st_mode) && (!executable || (info.st_mode & S_IXUSR) != 0);
}

static bool scripting_addition_parent_is_safe(void)
{
    return scripting_addition_path_is_safe("/Library", true, false) &&
           scripting_addition_path_is_safe("/Library/ScriptingAdditions", true, false);
}

static int scripting_addition_lock(void)
{
    if (!scripting_addition_parent_is_safe()) return -1;
    int fd = open("/Library/ScriptingAdditions/.yabai-install.lock",
                  O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd == -1) return -1;

    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
        (info.st_mode & 022) != 0 || flock(fd, LOCK_EX) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool scripting_addition_bundle_is_safe(void)
{
    const char *dirs[] = {
        osax_base_dir, osax_contents_dir, osax_contents_macos_dir, osax_contents_res_dir,
        osax_payload_dir, osax_payload_contents_dir, osax_payload_contents_macos_dir
    };
    const char *files[] = { osax_info_plist, osax_payload_plist };

    if (!scripting_addition_parent_is_safe()) return false;
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        if (!scripting_addition_path_is_safe(dirs[i], true, false)) return false;
    }
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        if (!scripting_addition_path_is_safe(files[i], false, false)) return false;
    }
    return scripting_addition_path_is_safe(osax_bin_loader, false, true) &&
           scripting_addition_path_is_safe(osax_bin_payload, false, true);
}

static int scripting_addition_check(void)
{
    bool result = 0;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    if (scripting_addition_is_installed() && scripting_addition_bundle_is_safe()) {
        NSString *payload_path = [NSString stringWithUTF8String:osax_payload_dir];
        NSBundle *payload_bundle = [NSBundle bundleWithPath:payload_path];
        NSString *ns_version = [payload_bundle objectForInfoDictionaryKey:@"CFBundleVersion"];

        bool status = string_equals([ns_version UTF8String], OSAX_VERSION);
        result = status && yabai_socket_installed_requirement_matches_self() ? 0 : 1;
    } else {
        result = 1;
    }

    [pool drain];
    return result;
}

static bool scripting_addition_remove(void)
{
    char *argv[] = { "/bin/rm", "-rf", osax_base_dir, NULL };
    return scripting_addition_run_command(argv[0], argv);
}

static int scripting_addition_install(void)
{
    umask(S_IWGRP | S_IWOTH);
    if (!scripting_addition_parent_is_safe()) {
        warn("yabai: scripting-addition parent directory is not root-owned and protected!\n");
        return 1;
    }

    if ((scripting_addition_is_installed()) && (!scripting_addition_remove())) {
        return 1;
    }

    if (!scripting_addition_create_directory()) {
        goto cleanup;
    }

    if (!scripting_addition_write_file(sa_plist, strlen(sa_plist), osax_info_plist, "w")) {
        goto cleanup;
    }

    if (!scripting_addition_write_file(sa_bundle_plist, strlen(sa_bundle_plist), osax_payload_plist, "w")) {
        goto cleanup;
    }

    if (!scripting_addition_write_file((char *) __src_osax_loader, __src_osax_loader_len, osax_bin_loader, "wb")) {
        goto cleanup;
    }

    if (!scripting_addition_write_file((char *) __src_osax_payload, __src_osax_payload_len, osax_bin_payload, "wb")) {
        goto cleanup;
    }

    if (!yabai_socket_install_self_requirement()) {
        goto cleanup;
    }

    if (!scripting_addition_prepare_binaries("/usr/bin/codesign")) {
        warn("yabai: could not set executable permissions or sign the scripting-addition binaries!\n");
        goto cleanup;
    }
    scripting_addition_restart_dock();
    return 0;

cleanup:
    scripting_addition_remove();
    return 2;
}

//
// NOTE: The handshake reply is the payload's version, a NUL and its attribute
// bits. Whatever listens on the payload socket writes it and `--load-sa` reads
// it as root, so we read it only within the bytes that arrived, and give up on
// a listener that has not answered in a few seconds; a payload that has just
// loaded answers once it has looked up Dock's internals.
//

#define SA_HANDSHAKE_TIMEOUT_SECONDS 5

static bool scripting_addition_parse_handshake(const char *reply, size_t length, char *version, size_t version_size, uint32_t *attrib)
{
    const char *zero = memchr(reply, '\0', length);
    if (!zero) return false;

    size_t version_length = zero - reply;
    if (version_length >= version_size) return false;
    if (length - version_length - 1 < sizeof(uint32_t)) return false;

    memcpy(version, reply, version_length + 1);
    memcpy(attrib, zero + 1, sizeof(uint32_t));
    return true;
}

static bool scripting_addition_is_dock_peer(int sockfd)
{
    audit_token_t token;
    socklen_t size = sizeof(token);
    if (getsockopt(sockfd, SOL_LOCAL, LOCAL_PEERTOKEN, &token, &size) != 0 || size != sizeof(token)) return false;

    NSArray *dock = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.dock"];
    if (dock.count != 1 || ![dock[0] isFinishedLaunching] ||
        audit_token_to_pid(token) != [dock[0] processIdentifier]) return false;

    uid_t login_uid;
    if (!scripting_addition_login_uid(&login_uid) || audit_token_to_euid(token) != login_uid) return false;

    // The socket pathname belongs to the login user. Only Dock's Apple-signed
    // process may supply a successful root-side validation handshake.
    SecRequirementRef requirement = NULL;
    if (SecRequirementCreateWithString(CFSTR("anchor apple and identifier \"com.apple.dock\""),
                                       kSecCSDefaultFlags, &requirement) != errSecSuccess) return false;
    bool trusted = yabai_socket_peer_is_trusted(sockfd, login_uid, requirement);
    CFRelease(requirement);
    return trusted;
}

static bool scripting_addition_read_handshake(int sockfd, char *version, size_t version_size,
                                               uint32_t *attrib, unsigned timeout_seconds)
{
    char reply[BUFSIZ];
    uint64_t deadline;
    if (!yabai_socket_deadline_after_seconds(timeout_seconds, &deadline)) return false;

    size_t length = 0;
    while (length < sizeof(reply)) {
        ssize_t count = yabai_socket_recv_before_deadline(sockfd, reply + length,
                                                          sizeof(reply) - length, deadline);
        if (count < 0) return false;
        if (count == 0) return scripting_addition_parse_handshake(reply, length, version, version_size, attrib);
        length += (size_t) count;
    }
    return false;
}

static bool scripting_addition_request_handshake(char *version, size_t version_size, uint32_t *attrib)
{
    int sockfd;
    bool result = false;
    char bytes[] = { 0x01, 0x00, SA_OPCODE_HANDSHAKE };

    if (socket_open(&sockfd)) {
        // The payload closes the connection after its reply.
        if (socket_connect(sockfd, g_sa_socket_file) && scripting_addition_is_dock_peer(sockfd) &&
            send(sockfd, bytes, sizeof(bytes), MSG_NOSIGNAL) == sizeof(bytes)) {
            result = scripting_addition_read_handshake(sockfd, version, version_size, attrib,
                                                        SA_HANDSHAKE_TIMEOUT_SECONDS);
        }

        socket_close(sockfd);
    }

    return result;
}

static uint32_t scripting_addition_expected_attrib(void)
{
    //
    // NOTE: macOS 27 removed DPDesktopPictureManager, whose work the move_space lookup
    // now covers, and the payload no longer looks up setFrontWindow, which yabai does not use.
    //

    if ([[NSProcessInfo processInfo] operatingSystemVersion].majorVersion >= 27) {
        return OSAX_ATTRIB_ALL & ~(OSAX_ATTRIB_DPPM | OSAX_ATTRIB_SET_WINDOW);
    }

    return OSAX_ATTRIB_ALL;
}

static int scripting_addition_perform_validation(void)
{
    uint32_t attrib = 0;
    char version[SA_SOCKET_BUFF_LEN] = {0};
    bool is_latest_version_installed = scripting_addition_check() == 0;

    if (!scripting_addition_request_handshake(version, sizeof(version), &attrib)) {
        notify("scripting-addition", "connection failed!");
        return 1;
    }

    if (string_equals(version, OSAX_VERSION)) {
        uint32_t expected = scripting_addition_expected_attrib();
        if ((attrib & expected) == expected) {
            notify("scripting-addition", "payload v%s", version);
            return 0;
        }

        notify("scripting-addition", "payload (0x%X) doesn't support this macOS version!", attrib);
        return 1;
    }

    if (!is_latest_version_installed) {
        notify("scripting-addition", "payload is outdated, updating..");
        if (scripting_addition_install() != 0) return 1;
        notify("scripting-addition", "installed; retry --load-sa after Dock restarts");
        return 1;
    }

    notify("scripting-addition", "payload is outdated, restarting Dock.app..");
    scripting_addition_restart_dock();
    return 1;
}

bool scripting_addition_is_sip_friendly(void)
{
    uint32_t config = 0;
    csr_get_active_config(&config);

    if (!(config & CSR_ALLOW_UNRESTRICTED_FS)) {
        return false;
    }

    if (!(config & CSR_ALLOW_TASK_FOR_PID)) {
        return false;
    }

    return true;
}

#ifdef __arm64__
static bool scripting_addition_is_arm64e_enabled(void)
{
    char bootargs[2048];
    size_t len = sizeof(bootargs) - 1;

    if (sysctlbyname("kern.bootargs", bootargs, &len, NULL, 0) == 0) {
        if (strnstr(bootargs, "-arm64e_preview_abi", len)) {
            return true;
        }
    }

    return false;
}
#endif

static bool mach_loader_inject_payload(void)
{
    FILE *handle = popen("/Library/ScriptingAdditions/yabai.osax/Contents/MacOS/loader", "r");
    if (!handle) return false;

    int result = pclose(handle);
    if (WIFEXITED(result)) {
        return WEXITSTATUS(result) == 0;
    } else if (WIFSIGNALED(result)) {
        return false;
    } else if (WIFSTOPPED(result)) {
        return false;
    }

    return false;
}

int scripting_addition_uninstall(void)
{
    if (!scripting_addition_is_sip_friendly()) {
        warn("yabai: System Integrity Protection: Filesystem Protections and Debugging Restrictions must be disabled!\n");
        notify("scripting-addition", "System Integrity Protection: Filesystem Protections and Debugging Restrictions must be disabled!");
        return 1;
    }

    if (!is_root()) {
        warn("yabai: scripting-addition must be uninstalled as root!\n");
        notify("scripting-addition", "must be uninstalled as root!");
        return 1;
    }

    int lockfd = scripting_addition_lock();
    if (lockfd == -1) return 1;
    int result = !scripting_addition_is_installed() || scripting_addition_remove() ? 0 : 1;
    close(lockfd);
    return result;
}

int scripting_addition_load(void)
{
    int result = 0;
    int lockfd = -1;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    if (!is_root()) {
        warn("yabai: scripting-addition must be loaded as root!\n");
        notify("scripting-addition", "must be loaded as root!");
        result = 1;
        goto out;
    }

    if (!scripting_addition_is_sip_friendly()) {
        warn("yabai: System Integrity Protection: Filesystem Protections and Debugging Restrictions must be disabled!\n");
        notify("scripting-addition", "System Integrity Protection: Filesystem Protections and Debugging Restrictions must be disabled!");
        result = 1;
        goto out;
    }

    if (!scripting_addition_set_socket_path()) {
        warn("yabai: could not determine the logged-in user whose Dock gets the scripting-addition!\n");
        notify("scripting-addition", "could not determine the logged-in user!");
        result = 1;
        goto out;
    }

    lockfd = scripting_addition_lock();
    if (lockfd == -1) {
        warn("yabai: could not lock the scripting-addition installation!\n");
        notify("scripting-addition", "could not lock the installation!");
        result = 1;
        goto out;
    }

    if (scripting_addition_check() != 0) {
        result = scripting_addition_install();
        if (result == 0) {
            notify("scripting-addition", "installed; retry --load-sa after Dock restarts");
            result = 1;
        }
        goto out;
    }

#ifdef __arm64__
    if (!scripting_addition_is_arm64e_enabled()) {
        warn("yabai: missing required nvram boot-arg '-arm64e_preview_abi'!\n");
        notify("scripting-addition", "missing required nvram boot-arg '-arm64e_preview_abi'!");
        result = 1;
        goto out;
    }
#endif

    if (!mach_loader_inject_payload()) {
        warn("yabai: scripting-addition failed to inject payload into Dock.app!\n");
        notify("scripting-addition", "failed to inject payload into Dock.app!");
        result = 1;
        goto out;
    }

    result = scripting_addition_perform_validation();

out:
    if (lockfd != -1) close(lockfd);
    [pool drain];
    return result;
}

// A request is built in one buffer of the payload's message size. One that
// does not fit, such as a window list of about a thousand windows, is not
// sent, and its caller sees the failure of a Dock that did not answer.
#define sa_payload_init() char bytes[SA_SOCKET_BUFF_LEN]; int16_t length = 1+sizeof(length); bool fits = true
#define pack(v) do { if (length + sizeof(v) <= sizeof(bytes)) { memcpy(bytes+length, &v, sizeof(v)); length += sizeof(v); } else { fits = false; } } while (0)
#define sa_payload_send(op) (fits ? (*(int16_t*)bytes = length-sizeof(length), bytes[sizeof(length)] = op, scripting_addition_send_bytes(bytes, length)) : scripting_addition_refuse(__FUNCTION__))

static bool scripting_addition_refuse(const char *function)
{
    warn("%s: request exceeds %d bytes and was not sent\n", function, SA_SOCKET_BUFF_LEN);
    return false;
}

static bool scripting_addition_send_bytes(char *bytes, int length)
{
    int sockfd;
    char dummy;
    bool result = false;

    if (socket_open(&sockfd)) {
        // A Dock that stops answering must not hold the event loop.
        struct timeval timeout = { .tv_sec = 1 };
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        if (socket_connect(sockfd, g_sa_socket_file)) {
            if (send(sockfd, bytes, length, 0) != -1) {
                ssize_t received = recv(sockfd, &dummy, 1, 0);
                uint8_t opcode = bytes[sizeof(int16_t)];
                bool replies = opcode == SA_OPCODE_WINDOW_OPACITY_BATCH;
                result = !replies || (received == 1 && dummy == 'k');
            }
        }

        socket_close(sockfd);
    }

    return result;
}

bool scripting_addition_focus_space(uint64_t sid)
{
    sa_payload_init();
    pack(sid);
    return sa_payload_send(SA_OPCODE_SPACE_FOCUS);
}

bool scripting_addition_create_space(uint64_t sid)
{
    sa_payload_init();
    pack(sid);
    return sa_payload_send(SA_OPCODE_SPACE_CREATE);
}

bool scripting_addition_destroy_space(uint64_t sid)
{
    sa_payload_init();
    pack(sid);
    return sa_payload_send(SA_OPCODE_SPACE_DESTROY);
}

bool scripting_addition_move_space_to_display(uint64_t src_sid, uint64_t dst_sid, uint64_t src_prev_sid, bool focus)
{
    sa_payload_init();
    pack(src_sid);
    pack(dst_sid);
    pack(src_prev_sid);
    pack(focus);
    return sa_payload_send(SA_OPCODE_SPACE_MOVE);
}

bool scripting_addition_move_space_after_space(uint64_t src_sid, uint64_t dst_sid, bool focus)
{
    uint64_t dummy_sid = 0;
    sa_payload_init();
    pack(src_sid);
    pack(dst_sid);
    pack(dummy_sid);
    pack(focus);
    return sa_payload_send(SA_OPCODE_SPACE_MOVE);
}

bool scripting_addition_move_window(uint32_t wid, int x, int y)
{
    sa_payload_init();
    pack(wid);
    pack(x);
    pack(y);
    return sa_payload_send(SA_OPCODE_WINDOW_MOVE);
}

bool scripting_addition_set_opacity(uint32_t wid, float opacity, float duration)
{
    sa_payload_init();
    pack(wid);
    pack(opacity);
    pack(duration);
    return sa_payload_send(duration > 0.0f ? SA_OPCODE_WINDOW_OPACITY_FADE : SA_OPCODE_WINDOW_OPACITY);
}

#include "sa_opacity.c"

bool scripting_addition_set_layer(uint32_t wid, int layer)
{
    sa_payload_init();
    pack(wid);
    pack(layer);
    return sa_payload_send(SA_OPCODE_WINDOW_LAYER);
}

bool scripting_addition_set_sticky(uint32_t wid, bool sticky)
{
    sa_payload_init();
    pack(wid);
    pack(sticky);
    return sa_payload_send(SA_OPCODE_WINDOW_STICKY);
}

bool scripting_addition_set_shadow(uint32_t wid, bool shadow)
{
    sa_payload_init();
    pack(wid);
    pack(shadow);
    return sa_payload_send(SA_OPCODE_WINDOW_SHADOW);
}

bool scripting_addition_focus_window(uint32_t wid)
{
    sa_payload_init();
    pack(wid);
    return sa_payload_send(SA_OPCODE_WINDOW_FOCUS);
}

bool scripting_addition_scale_window(uint32_t wid, float x, float y, float w, float h)
{
    sa_payload_init();
    pack(wid);
    pack(x);
    pack(y);
    pack(w);
    pack(h);
    return sa_payload_send(SA_OPCODE_WINDOW_SCALE);
}

bool scripting_addition_swap_window_proxy_in(struct window_animation *animation_list, int animation_count)
{
    uint32_t dummy_wid = 0;
    sa_payload_init();
    pack(animation_count);
    for (int i = 0; i < animation_count; ++i) {
        if (__atomic_load_n(&animation_list[i].skip, __ATOMIC_RELAXED)) {
            pack(dummy_wid);
        } else {
            pack(animation_list[i].wid);
            pack(animation_list[i].proxy.id);
        }
    }
    return sa_payload_send(SA_OPCODE_WINDOW_SWAP_PROXY_IN);
}

bool scripting_addition_swap_window_proxy_out(struct window_animation *animation_list, int animation_count)
{
    uint32_t dummy_wid = 0;
    sa_payload_init();
    pack(animation_count);
    for (int i = 0; i < animation_count; ++i) {
        if (__atomic_load_n(&animation_list[i].skip, __ATOMIC_RELAXED)) {
            pack(dummy_wid);
        } else {
            pack(animation_list[i].wid);
            pack(animation_list[i].proxy.id);
        }
    }
    return sa_payload_send(SA_OPCODE_WINDOW_SWAP_PROXY_OUT);
}

bool scripting_addition_order_window(uint32_t a_wid, int order, uint32_t b_wid)
{
    sa_payload_init();
    pack(a_wid);
    pack(order);
    pack(b_wid);
    return sa_payload_send(SA_OPCODE_WINDOW_ORDER);
}

bool scripting_addition_order_window_in(uint32_t *window_list, int window_count)
{
    uint32_t dummy_wid = 0;
    uint8_t ordered_in = 0;

    sa_payload_init();
    pack(window_count);
    for (int i = 0; i < window_count; ++i) {
        SLSWindowIsOrderedIn(g_connection, window_list[i], &ordered_in);
        if (ordered_in) {
            pack(dummy_wid);
        } else {
            pack(window_list[i]);
        }
    }
    return sa_payload_send(SA_OPCODE_WINDOW_ORDER_IN);
}

bool scripting_addition_move_window_list_to_space(uint64_t sid, uint32_t *window_list, int window_count)
{
    sa_payload_init();
    pack(sid);
    pack(window_count);
    for (int i = 0; i < window_count; ++i) {
        pack(window_list[i]);
    }
    return sa_payload_send(SA_OPCODE_WINDOW_LIST_TO_SPACE);
}

bool scripting_addition_move_window_to_space(uint64_t sid, uint32_t wid)
{
    sa_payload_init();
    pack(sid);
    pack(wid);
    return sa_payload_send(SA_OPCODE_WINDOW_TO_SPACE);
}

#undef sa_payload_init
#undef pack
#undef sa_payload_send
