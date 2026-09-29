#ifndef YABAI_SOCKET_IDENTITY_H
#define YABAI_SOCKET_IDENTITY_H

// Authenticate local socket peers against the daemon's designated requirement.
// The daemon uses its own signature; the Dock payload reads the root-owned
// requirement installed beside it. Socket accept threads call these helpers.

#include <Security/Security.h>
#include <bsm/libbsm.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef YABAI_ALLOW_UNSIGNED_LOCAL
#define YABAI_ALLOW_UNSIGNED_LOCAL 0
#endif

#define YABAI_SOCKET_REQUIREMENT_FILE \
    "/Library/ScriptingAdditions/yabai.osax/Contents/Resources/daemon.requirement"
#define YABAI_SOCKET_UNSIGNED_MARKER "unsigned-local-v1"

static inline CFDataRef yabai_socket_copy_self_requirement_data(void)
{
    SecCodeRef code = NULL;
    SecStaticCodeRef static_code = NULL;
    SecRequirementRef requirement = NULL;
    CFDictionaryRef signing_info = NULL;
    CFDataRef data = NULL;

    if (SecCodeCopySelf(kSecCSDefaultFlags, &code) != errSecSuccess) goto out;
    if (SecCodeCopyStaticCode(code, kSecCSDefaultFlags, &static_code) != errSecSuccess) goto out;
    if (SecStaticCodeCheckValidity(static_code, kSecCSCheckAllArchitectures, NULL) != errSecSuccess) goto out;
    if (SecCodeCopySigningInformation(code, kSecCSSigningInformation, &signing_info) != errSecSuccess) goto out;
    CFArrayRef certificates = CFDictionaryGetValue(signing_info, kSecCodeInfoCertificates);
    if (!certificates || CFGetTypeID(certificates) != CFArrayGetTypeID() ||
        CFArrayGetCount(certificates) == 0) goto out;
    if (SecCodeCopyDesignatedRequirement(static_code, kSecCSDefaultFlags, &requirement) != errSecSuccess) goto out;
    if (SecRequirementCopyData(requirement, kSecCSDefaultFlags, &data) != errSecSuccess) data = NULL;

out:
    if (signing_info) CFRelease(signing_info);
    if (requirement) CFRelease(requirement);
    if (static_code) CFRelease(static_code);
    if (code) CFRelease(code);
    return data;
}

static inline CFDataRef yabai_socket_copy_installed_requirement_data(void)
{
    int fd = open(YABAI_SOCKET_REQUIREMENT_FILE, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) return NULL;

    struct stat info;
    CFDataRef data = NULL;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
        (info.st_mode & 022) != 0 || info.st_size <= 0 || info.st_size > 8192) goto out;

    UInt8 bytes[8192];
    size_t length = (size_t) info.st_size;
    size_t consumed = 0;
    while (consumed < length) {
        ssize_t count = read(fd, bytes + consumed, length - consumed);
        if (count <= 0) goto out;
        consumed += (size_t) count;
    }

    data = CFDataCreate(kCFAllocatorDefault, bytes, length);

out:
    close(fd);
    return data;
}

static inline SecRequirementRef yabai_socket_requirement_from_data(CFDataRef data)
{
    SecRequirementRef requirement = NULL;
    if (!data || SecRequirementCreateWithData(data, kSecCSDefaultFlags, &requirement) != errSecSuccess) {
        return NULL;
    }
    return requirement;
}

static inline bool yabai_socket_install_self_requirement(void)
{
    if (geteuid() != 0) return false;

    CFDataRef data = YABAI_ALLOW_UNSIGNED_LOCAL
                   ? CFDataCreate(kCFAllocatorDefault, (const UInt8 *) YABAI_SOCKET_UNSIGNED_MARKER,
                                  sizeof(YABAI_SOCKET_UNSIGNED_MARKER) - 1)
                   : yabai_socket_copy_self_requirement_data();
    if (!data || CFDataGetLength(data) > 8192) {
        if (data) CFRelease(data);
        return false;
    }

    int fd = open(YABAI_SOCKET_REQUIREMENT_FILE, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    bool result = false;
    if (fd != -1) {
        const UInt8 *bytes = CFDataGetBytePtr(data);
        size_t length = (size_t) CFDataGetLength(data);
        size_t written = 0;
        while (written < length) {
            ssize_t count = write(fd, bytes + written, length - written);
            if (count <= 0) break;
            written += (size_t) count;
        }
        result = written == length && fchmod(fd, 0644) == 0;
        close(fd);
    }
    CFRelease(data);
    return result;
}

static inline bool yabai_socket_installed_requirement_matches_self(void)
{
    CFDataRef expected = YABAI_ALLOW_UNSIGNED_LOCAL
                       ? CFDataCreate(kCFAllocatorDefault, (const UInt8 *) YABAI_SOCKET_UNSIGNED_MARKER,
                                      sizeof(YABAI_SOCKET_UNSIGNED_MARKER) - 1)
                       : yabai_socket_copy_self_requirement_data();
    CFDataRef installed = yabai_socket_copy_installed_requirement_data();
    bool matches = expected && installed && CFEqual(expected, installed);
    if (installed) CFRelease(installed);
    if (expected) CFRelease(expected);
    return matches;
}

static inline bool yabai_socket_peer_is_trusted(int fd, uid_t owner, SecRequirementRef requirement)
{
    audit_token_t token;
    socklen_t size = sizeof(token);
    if (getsockopt(fd, SOL_LOCAL, LOCAL_PEERTOKEN, &token, &size) != 0 || size != sizeof(token)) return false;

    uid_t peer = audit_token_to_euid(token);
    if (peer != owner && peer != 0) return false;
    if (YABAI_ALLOW_UNSIGNED_LOCAL) return true;
    if (!requirement) return false;

    CFDataRef audit = CFDataCreate(kCFAllocatorDefault, (const UInt8 *) &token, sizeof(token));
    if (!audit) return false;
    const void *keys[] = { kSecGuestAttributeAudit };
    const void *values[] = { audit };
    CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                                                    &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
    CFRelease(audit);
    if (!attributes) return false;

    SecCodeRef code = NULL;
    bool trusted = SecCodeCopyGuestWithAttributes(NULL, attributes, kSecCSDefaultFlags, &code) == errSecSuccess &&
                   SecCodeCheckValidity(code, kSecCSDefaultFlags, requirement) == errSecSuccess;
    if (code) CFRelease(code);
    CFRelease(attributes);
    return trusted;
}

#endif
