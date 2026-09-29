// Compiles the scripting-addition client as one daemon translation unit.
// The manifest still includes these sources for isolated tests and fuzzing.
#include <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <bootstrap.h>
#ifdef __x86_64__
#include <emmintrin.h>
#elif __arm64__
#include <arm_neon.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pwd.h>
#include <regex.h>
#include <sys/mman.h>

#include "../misc/extern.h"
#include "../misc/macros.h"
#include "../misc/ts.h"
#include "../misc/notify.h"
#include "../misc/log.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "../misc/helpers.h"
#include "../spaces/view.h"
#pragma clang diagnostic pop
#include "sa.m"
