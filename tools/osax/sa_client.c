//
// Talks to the scripting-addition socket inside Dock.app directly, without a
// running yabai, to test a payload after it has been injected.
//
// usage: sa_client handshake        print the payload version and attributes
//        sa_client spaces           list spaces per display (read-only)
//        sa_client focus <sid>      focus a space
//        sa_client create <sid>     create a space on the display of <sid>
//        sa_client destroy <sid>    destroy a space
//        sa_client move <sid> <dst> move a space after <dst>, on the display of <dst>
//
// focus, create, destroy and move change the space layout; space ids come from
// `sa_client spaces` or `yabai -m query --spaces`. move does not switch spaces,
// so move a space that is not the current one of its display.
//

#include <CoreFoundation/CoreFoundation.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "common.h"
#include "socket_path.h"

extern int SLSMainConnectionID(void);
extern CFArrayRef SLSCopyManagedDisplaySpaces(int cid);
extern uint64_t SLSGetActiveSpace(int cid);

static const char *attrib_names[] =
{
    "dock_spaces", "dppm", "add_space", "remove_space", "move_space", "set_front_window", "animation_time",
};

static int sa_connect(void)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    if (!yabai_socket_path(getuid(), YABAI_SOCKET_PAYLOAD,
                           addr.sun_path, sizeof(addr.sun_path), false)) {
        fprintf(stderr, "private payload socket directory is unavailable\n");
        exit(1);
    }

    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1 || connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        perror(addr.sun_path);
        exit(1);
    }

    return sockfd;
}

static void sa_send(uint8_t opcode, const void *payload, int16_t payload_length, char *reply, int reply_size)
{
    char message[64];
    int16_t length = 1 + payload_length;
    memcpy(message, &length, sizeof(length));
    message[sizeof(length)] = opcode;
    memcpy(message + sizeof(length) + 1, payload, payload_length);

    int sockfd = sa_connect();
    if (send(sockfd, message, sizeof(length) + length, 0) == -1) {
        perror("send");
        exit(1);
    }

    int received = recv(sockfd, reply, reply_size - 1, 0);
    reply[received > 0 ? received : 0] = '\0';
    close(sockfd);
}

static void do_handshake(void)
{
    char reply[256] = {0};
    sa_send(SA_OPCODE_HANDSHAKE, NULL, 0, reply, sizeof(reply));

    uint32_t attrib = 0;
    size_t version_length = strlen(reply);
    memcpy(&attrib, reply + version_length + 1, sizeof(attrib));

    printf("version %s attrib 0x%02X\n", reply, attrib);
    for (size_t i = 0; i < sizeof(attrib_names) / sizeof(*attrib_names); ++i) {
        printf("  %-17s %s\n", attrib_names[i], attrib & (1u << i) ? "yes" : "no");
    }
}

static uint64_t cfnumber_u64(CFDictionaryRef dict, CFStringRef key)
{
    uint64_t value = 0;
    CFNumberRef number = CFDictionaryGetValue(dict, key);
    if (number) CFNumberGetValue(number, kCFNumberSInt64Type, &value);
    return value;
}

static void do_spaces(void)
{
    int cid = SLSMainConnectionID();
    printf("active %llu\n", SLSGetActiveSpace(cid));

    CFArrayRef displays = SLSCopyManagedDisplaySpaces(cid);
    if (!displays) return;

    for (CFIndex i = 0; i < CFArrayGetCount(displays); ++i) {
        CFDictionaryRef display = CFArrayGetValueAtIndex(displays, i);
        CFDictionaryRef current = CFDictionaryGetValue(display, CFSTR("Current Space"));
        CFArrayRef spaces = CFDictionaryGetValue(display, CFSTR("Spaces"));

        char uuid[64] = {0};
        CFStringRef uuid_ref = CFDictionaryGetValue(display, CFSTR("Display Identifier"));
        if (uuid_ref) CFStringGetCString(uuid_ref, uuid, sizeof(uuid), kCFStringEncodingUTF8);

        printf("display %s current %llu spaces", uuid, current ? cfnumber_u64(current, CFSTR("id64")) : 0);
        for (CFIndex j = 0; spaces && j < CFArrayGetCount(spaces); ++j) {
            CFDictionaryRef space = CFArrayGetValueAtIndex(spaces, j);
            printf(" %llu%s", cfnumber_u64(space, CFSTR("id64")), cfnumber_u64(space, CFSTR("type")) == 4 ? "(fullscreen)" : "");
        }
        printf("\n");
    }

    CFRelease(displays);
}

int main(int argc, char **argv)
{
    const char *command = argc > 1 ? argv[1] : "";

    if (strcmp(command, "handshake") == 0) {
        do_handshake();
        return 0;
    }

    if (strcmp(command, "spaces") == 0) {
        do_spaces();
        return 0;
    }

    uint8_t opcode = 0;
    if      (strcmp(command, "focus") == 0)   opcode = SA_OPCODE_SPACE_FOCUS;
    else if (strcmp(command, "create") == 0)  opcode = SA_OPCODE_SPACE_CREATE;
    else if (strcmp(command, "destroy") == 0) opcode = SA_OPCODE_SPACE_DESTROY;
    else if (strcmp(command, "move") == 0)    opcode = SA_OPCODE_SPACE_MOVE;

    if (!opcode || argc < (opcode == SA_OPCODE_SPACE_MOVE ? 4 : 3)) {
        fprintf(stderr, "usage: %s handshake | spaces | focus <sid> | create <sid> | destroy <sid> | move <sid> <dst>\n", argv[0]);
        return 2;
    }

    // Space move arguments: source, destination, source's previous space, focus.
    char payload[3 * sizeof(uint64_t) + 1] = {0};
    uint64_t sid = strtoull(argv[2], NULL, 10);
    memcpy(payload, &sid, sizeof(sid));

    int16_t payload_length = sizeof(sid);
    if (opcode == SA_OPCODE_SPACE_MOVE) {
        uint64_t dst_sid = strtoull(argv[3], NULL, 10);
        memcpy(payload + sizeof(sid), &dst_sid, sizeof(dst_sid));
        payload_length = sizeof(payload);
    }

    char reply[16];
    sa_send(opcode, payload, payload_length, reply, sizeof(reply));

    // NOTE: Dock applies space changes asynchronously after acknowledging.
    usleep(300000);
    do_spaces();
    return 0;
}
