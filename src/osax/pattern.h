#ifndef SA_PATTERN_H
#define SA_PATTERN_H

#include <stdint.h>
#include <string.h>

//
// Byte-pattern search used to locate private Dock.app symbols. Shared by the
// payload and tools/osax/pattern_check so both resolve addresses identically.
//
// A pattern is a string of two-character hex tokens separated by one space.
// A token whose first character is '?' matches any byte, so "?8" is a full
// wildcard just like "??". The search starts at baddr and returns the first
// match that starts less than PATTERN_SEARCH_WINDOW bytes after it and ends
// at or before end, or 0. It reads no byte at or past end.
//

#define PATTERN_SEARCH_WINDOW 0x1286a0

static uint64_t hex_find_seq(uint64_t baddr, uint64_t end, const char *c_pattern)
{
    if (!baddr || !c_pattern) return 0;

    uint64_t addr = baddr;
    uint64_t pattern_length = (strlen(c_pattern) + 1) / 3;
    if (!pattern_length || baddr >= end || end - baddr < pattern_length) return 0;

    uint64_t last = end - pattern_length;
    char buffer_a[pattern_length];
    char buffer_b[pattern_length];
    memset(buffer_a, 0, sizeof(buffer_a));
    memset(buffer_b, 0, sizeof(buffer_b));

    char *pattern = (char *) c_pattern + 1;
    for (uint64_t i = 0; i < pattern_length; ++i) {
        char c = pattern[-1];
        if (c == '?') {
            buffer_b[i] = 1;
        } else {
            int temp = c <= '9' ? 0 : 9;
            temp = (temp + c) << 0x4;
            c = pattern[0];
            int temp2 = c <= '9' ? 0xd0 : 0xc9;
            buffer_a[i] = temp2 + c + temp;
        }
        pattern += 3;
    }

loop:
    for (uint64_t counter = 0; counter < pattern_length; ++counter) {
        if ((buffer_b[counter] == 0) && (((char *)addr)[counter] != buffer_a[counter])) {
            addr = (uint64_t)((char *)addr + 1);
            if (addr - baddr < PATTERN_SEARCH_WINDOW && addr <= last) {
                goto loop;
            } else {
                return 0;
            }
        }
    }

    return addr;
}

#if __arm64__
//
// Resolves the target of an adrp+add pair at addr, whose offset from the
// image base is offset. Returns the target as an offset from the image base.
//
static uint64_t decode_adrp_add(uint64_t addr, uint64_t offset)
{
    uint32_t adrp_instr = *(uint32_t *) addr;

    uint32_t immlo = (0x60000000 & adrp_instr) >> 29;
    uint32_t immhi = (0xffffe0 & adrp_instr) >> 3;

    int32_t value = (immhi | immlo) << 12;
    int64_t value_64 = value;

    uint32_t add_instr = *(uint32_t *) (addr + 4);
    uint64_t imm12 = (add_instr & 0x3ffc00) >> 10;

    if (add_instr & 0xc00000) {
        imm12 <<= 12;
    }

    return (offset & 0xfffffffffffff000) + value_64 + imm12;
}
#endif

#endif
