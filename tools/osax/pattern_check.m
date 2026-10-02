//
// Resolves the scripting-addition lookups against a Dock binary on disk,
// using the same patterns and search code as the payload. Every arm64 slice
// of the binary is checked, so both arm64e variants shipped since macOS 27 are
// covered even though a machine only ever runs one of them.
//
// Exits non-zero if a lookup the payload performs for the selected macOS
// version finds nothing, or finds something outside the expected section.
//
// usage: pattern_check [-v major[.minor]] [-p offset pattern] [dock]
//
//   -v   select the patterns for this macOS version instead of the running one
//   -p   search for one pattern instead, listing every match in __text
//

#import <Foundation/Foundation.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

#include "pattern.h"
#include "arm64_payload.m"

#define DOCK_PATH "/System/Library/CoreServices/Dock.app/Contents/MacOS/Dock"

enum lookup_kind
{
    LOOKUP_GLOBAL,
    LOOKUP_CODE,
};

struct lookup
{
    const char *name;
    enum lookup_kind kind;
    uint64_t (*offset)(NSOperatingSystemVersion);
    const char *(*pattern)(NSOperatingSystemVersion);
};

static struct lookup lookups[] =
{
    { "dock_spaces",      LOOKUP_GLOBAL, get_dock_spaces_offset,      get_dock_spaces_pattern      },
    { "dppm",             LOOKUP_GLOBAL, get_dppm_offset,             get_dppm_pattern             },
    { "add_space",        LOOKUP_CODE,   get_add_space_offset,        get_add_space_pattern        },
    { "remove_space",     LOOKUP_CODE,   get_remove_space_offset,     get_remove_space_pattern     },
    { "move_space",       LOOKUP_CODE,   get_move_space_offset,       get_move_space_pattern       },
    { "set_front_window", LOOKUP_CODE,   get_set_front_window_offset, get_set_front_window_pattern },
    { "animation",        LOOKUP_CODE,   get_fix_animation_offset,    get_fix_animation_pattern    },
};

struct slice
{
    uint8_t *base;
    uint64_t size;
    uint32_t cpusubtype;
    uint64_t text_start;
    uint64_t text_end;
    uint64_t text_vmaddr;
    const struct mach_header_64 *header;
};

static const struct section_64 *section_containing(struct slice *slice, uint64_t offset)
{
    const struct load_command *lc = (const struct load_command *)(slice->header + 1);
    for (uint32_t i = 0; i < slice->header->ncmds; ++i) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *) lc;
            const struct section_64 *sect = (const struct section_64 *)(seg + 1);
            for (uint32_t j = 0; j < seg->nsects; ++j, ++sect) {
                uint64_t start = sect->addr - slice->text_vmaddr;
                if (offset >= start && offset < start + sect->size) return sect;
            }
        }
        lc = (const struct load_command *)((const uint8_t *) lc + lc->cmdsize);
    }

    return NULL;
}

static bool slice_init(struct slice *slice, uint8_t *base, uint64_t size, uint32_t cpusubtype)
{
    const struct mach_header_64 *header = (const struct mach_header_64 *) base;
    if (size < sizeof(*header) || header->magic != MH_MAGIC_64) return false;

    slice->base = base;
    slice->size = size;
    slice->cpusubtype = cpusubtype;
    slice->header = header;

    const struct load_command *lc = (const struct load_command *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *) lc;
            if (strcmp(seg->segname, SEG_TEXT) == 0) slice->text_vmaddr = seg->vmaddr;
            const struct section_64 *sect = (const struct section_64 *)(seg + 1);
            for (uint32_t j = 0; j < seg->nsects; ++j, ++sect) {
                if (strcmp(sect->segname, SEG_TEXT) == 0 && strcmp(sect->sectname, SECT_TEXT) == 0) {
                    slice->text_start = sect->offset;
                    slice->text_end = sect->offset + sect->size;
                    return true;
                }
            }
        }
        lc = (const struct load_command *)((const uint8_t *) lc + lc->cmdsize);
    }

    return false;
}

static bool check_lookup(struct slice *slice, struct lookup *lookup, NSOperatingSystemVersion os_version)
{
    const char *pattern = lookup->pattern(os_version);
    if (!pattern) {
        printf("  %-17s unused on this version\n", lookup->name);
        return true;
    }

    uint64_t start = (uint64_t) slice->base + lookup->offset(os_version);
    uint64_t end = (uint64_t) slice->base + slice->text_end;
    uint64_t addr = hex_find_seq(start, end, pattern);
    if (!addr) {
        printf("  %-17s MISS (search from %#llx)\n", lookup->name, lookup->offset(os_version));
        return false;
    }

    uint64_t offset = addr - (uint64_t) slice->base;
    int matches = 0;
    for (uint64_t next = addr; next; next = hex_find_seq(next + 1, end, pattern)) {
        if (next - start >= PATTERN_SEARCH_WINDOW) break;
        ++matches;
    }

    if (offset < slice->text_start || offset >= slice->text_end) {
        printf("  %-17s FAIL %#llx is outside __TEXT,__text\n", lookup->name, offset);
        return false;
    }

    if (lookup->kind == LOOKUP_GLOBAL) {
        uint64_t target = decode_adrp_add(addr, offset);
        const struct section_64 *sect = section_containing(slice, target);
        if (!sect || strcmp(sect->segname, SEG_TEXT) == 0) {
            printf("  %-17s FAIL %#llx -> %#llx is not in a data section\n", lookup->name, offset, target);
            return false;
        }
        printf("  %-17s %#llx -> %#llx (%.16s,%.16s)", lookup->name, offset, target, sect->segname, sect->sectname);
    } else {
        printf("  %-17s %#llx", lookup->name, offset);
    }

    printf(matches > 1 ? "  [%d matches in window, first used]\n" : "\n", matches);
    return true;
}

static void list_matches(struct slice *slice, uint64_t offset, const char *pattern)
{
    uint64_t base = (uint64_t) slice->base;
    uint64_t end = base + slice->text_end;
    uint64_t first = hex_find_seq(base + offset, end, pattern);
    printf("  payload lookup from %#llx: %s", offset, first ? "" : "MISS\n");
    if (first) printf("%#llx\n", first - base);

    printf("  all matches in __text:");
    int count = 0;
    uint64_t cursor = base + slice->text_start;
    while (cursor < base + slice->text_end) {
        uint64_t addr = hex_find_seq(cursor, end, pattern);
        if (!addr) {
            cursor += PATTERN_SEARCH_WINDOW;
            continue;
        }
        if (addr >= base + slice->text_end) break;
        printf(" %#llx", addr - base);
        cursor = addr + 1;
        ++count;
    }
    printf("%s (%d)\n", count ? "" : " none", count);
}

static NSOperatingSystemVersion parse_version(const char *s)
{
    NSOperatingSystemVersion v = {0};
    sscanf(s, "%ld.%ld.%ld", &v.majorVersion, &v.minorVersion, &v.patchVersion);
    return v;
}

int main(int argc, char **argv)
{
    NSOperatingSystemVersion os_version = [[NSProcessInfo processInfo] operatingSystemVersion];
    const char *adhoc_pattern = NULL;
    uint64_t adhoc_offset = 0;

    int option;
    while ((option = getopt(argc, argv, "v:p:")) != -1) {
        if (option == 'v') {
            os_version = parse_version(optarg);
        } else if (option == 'p' && optind < argc) {
            adhoc_offset = strtoull(optarg, NULL, 0);
            adhoc_pattern = argv[optind++];
        } else {
            fprintf(stderr, "usage: %s [-v major[.minor]] [-p offset pattern] [dock]\n", argv[0]);
            return 2;
        }
    }

    const char *path = optind < argc ? argv[optind] : DOCK_PATH;
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd == -1 || fstat(fd, &st) == -1) {
        perror(path);
        return 2;
    }

    //
    // NOTE: The search may read up to one window past its last candidate, so
    // the file is mapped at the start of a larger zero-filled reservation.
    //

    size_t reserve = st.st_size + PATTERN_SEARCH_WINDOW + 0x10000;
    uint8_t *file = mmap(NULL, reserve, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (file == MAP_FAILED || mmap(file, st.st_size, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0) == MAP_FAILED) {
        perror("mmap");
        return 2;
    }

    struct slice slices[8];
    int slice_count = 0;

    const struct fat_header *fat = (const struct fat_header *) file;
    if (OSSwapBigToHostInt32(fat->magic) == FAT_MAGIC) {
        const struct fat_arch *arch = (const struct fat_arch *)(fat + 1);
        uint32_t count = OSSwapBigToHostInt32(fat->nfat_arch);
        for (uint32_t i = 0; i < count && slice_count < 8; ++i, ++arch) {
            if (OSSwapBigToHostInt32(arch->cputype) != CPU_TYPE_ARM64) continue;
            uint32_t offset = OSSwapBigToHostInt32(arch->offset);
            uint32_t size = OSSwapBigToHostInt32(arch->size);
            uint32_t subtype = OSSwapBigToHostInt32(arch->cpusubtype) & ~CPU_SUBTYPE_MASK;
            if (slice_init(&slices[slice_count], file + offset, size, subtype)) ++slice_count;
        }
    } else if (((const struct mach_header_64 *) file)->cputype == CPU_TYPE_ARM64) {
        uint32_t subtype = ((const struct mach_header_64 *) file)->cpusubtype & ~CPU_SUBTYPE_MASK;
        if (slice_init(&slices[0], file, st.st_size, subtype)) slice_count = 1;
    }

    if (slice_count == 0) {
        fprintf(stderr, "%s: no arm64 Mach-O slice found\n", path);
        return 2;
    }

    printf("%s\npatterns for macOS %ld.%ld\n", path, os_version.majorVersion, os_version.minorVersion);

    bool ok = true;
    for (int i = 0; i < slice_count; ++i) {
        printf("slice %d (cpusubtype %u, __text %#llx-%#llx)\n", i, slices[i].cpusubtype, slices[i].text_start, slices[i].text_end);
        if (adhoc_pattern) {
            list_matches(&slices[i], adhoc_offset, adhoc_pattern);
            continue;
        }
        for (size_t j = 0; j < sizeof(lookups) / sizeof(*lookups); ++j) {
            ok &= check_lookup(&slices[i], &lookups[j], os_version);
        }
    }

    return ok ? 0 : 1;
}
