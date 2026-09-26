//
// Finds the MIG message id that window.c sends to query a window's sub-level
// (SLSGetWindowSubLevel__Internal), which changes between macOS releases.
//
// usage: window_sub_level [id ...]
//
// First lists the MIG-range immediates loaded by SLSGetWindowSubLevel and the
// functions it calls. MIG replies carry the request id + 100, so an immediate
// in SLSGetWindowSubLevel itself is usually the reply id it checks for. Then
// sends each request id given (default: those derived from the reply ids) for
// a few on-screen windows. The right id replies with KERN_SUCCESS and the
// expected reply id, and its sub-levels match the public API.
//
// NOTE: Only pass ids that belong to a getter; a message id of a setter would
// modify the queried windows.
//

#include <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <mach/mig.h>
#include <ptrauth.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKYLIGHT_PATH "/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight"

static bool string_equals(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
#include "../../src/misc/macho_dlsym.h"

extern int SLSMainConnectionID(void);
extern int SLSGetWindowSubLevel(int cid, uint32_t wid);
extern mach_port_t mig_get_special_reply_port(void);

static mach_port_t (*CGSGetConnectionPortById)(int cid);

static int reply_ids[16];
static int reply_id_count;

static void scan_mig_ids(const char *name, const uint32_t *code, int depth)
{
    for (int i = 0; i < 256; ++i) {
        uint32_t insn = code[i];

        if ((insn & 0x7F800000) == 0x52800000) { // movz
            uint32_t imm = (insn >> 5) & 0xFFFF;
            if (imm >= 0x7000 && imm < 0x8000) {
                printf("  %*s%s+%#x: movz #%#x\n", depth * 2, "", name, i * 4, imm);
                if (depth == 0 && reply_id_count < 16) reply_ids[reply_id_count++] = imm;
            }
        }

        if ((insn & 0xFC000000) == 0x94000000 && depth == 0) { // bl
            const uint32_t *target = code + i + ((int32_t)(insn << 6) >> 6);
            Dl_info info = {0};
            dladdr(target, &info);
            scan_mig_ids(info.dli_sname ? info.dli_sname : "?", target, depth + 1);
        }

        if (insn == 0xD65F0FFF || insn == 0xD65F0BFF || insn == 0xD65F03C0) break; // retab, retaa, ret
    }
}

static int probe(int cid, uint32_t wid, int id, kern_return_t *kr, int *reply_id)
{
    #pragma pack(push,4)
    struct {
        mach_msg_header_t header;
        NDR_record_t NDR_record;
        uint32_t window_id;
        int32_t sub_level;
        int32_t padding1;
        int32_t padding2;
    } msg = {0};
    #pragma pack(pop)

    msg.NDR_record = NDR_record;
    msg.window_id = wid;
    msg.header.msgh_bits = 0x1513;
    msg.header.msgh_remote_port = CGSGetConnectionPortById(cid);
    msg.header.msgh_local_port = mig_get_special_reply_port();
    msg.header.msgh_id = id;
    *kr = mach_msg(&msg.header, MACH_SEND_MSG|MACH_RCV_MSG, 0x24, 0x30, msg.header.msgh_local_port, 0, 0);
    *reply_id = msg.header.msgh_id;

    return msg.sub_level;
}

int main(int argc, char **argv)
{
    CGSGetConnectionPortById = macho_find_symbol(SKYLIGHT_PATH, "_CGSGetConnectionPortById");
    if (!CGSGetConnectionPortById) {
        fprintf(stderr, "_CGSGetConnectionPortById not found in SkyLight\n");
        return 1;
    }

    printf("MIG-range immediates in SLSGetWindowSubLevel:\n");
    scan_mig_ids("SLSGetWindowSubLevel", ptrauth_strip((void *) SLSGetWindowSubLevel, ptrauth_key_asia), 0);

    int ids[16];
    int id_count = 0;
    if (argc > 1) {
        for (int i = 1; i < argc && id_count < 16; ++i) ids[id_count++] = (int) strtol(argv[i], NULL, 0);
    } else {
        for (int i = 0; i < reply_id_count; ++i) ids[id_count++] = reply_ids[i] - 100;
    }

    if (id_count == 0) {
        fprintf(stderr, "no reply id found in SLSGetWindowSubLevel; pass request ids explicitly\n");
        return 1;
    }

    int cid = SLSMainConnectionID();
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
    if (!windows) return 1;

    for (CFIndex i = 0; i < CFArrayGetCount(windows) && i < 8; ++i) {
        uint32_t wid = 0;
        CFNumberGetValue(CFDictionaryGetValue(CFArrayGetValueAtIndex(windows, i), kCGWindowNumber), kCFNumberSInt32Type, &wid);

        printf("window %-6u api %-4d", wid, SLSGetWindowSubLevel(cid, wid));
        for (int j = 0; j < id_count; ++j) {
            kern_return_t kr;
            int reply_id;
            int level = probe(cid, wid, ids[j], &kr, &reply_id);
            printf("  %#x: %s", ids[j], kr == KERN_SUCCESS && reply_id == ids[j] + 100 ? "ok" : "bad");
            printf(" level %-4d (kr %#x reply %#x)", level, kr, reply_id);
        }
        printf("\n");
    }

    CFRelease(windows);
    return 0;
}
