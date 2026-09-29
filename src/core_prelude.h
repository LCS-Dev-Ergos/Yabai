#ifndef CORE_PRELUDE_H
#define CORE_PRELUDE_H

// Shared system types, helpers and area interfaces for the daemon's units.
// Only the owning unit defines YABAI_DEFINE_CORE before inclusion.
// Private declarations and header-only helpers are intentionally unused by
// some area units; keep that warning scoped to this shared include surface.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include <objc/objc-runtime.h>
#include <Carbon/Carbon.h>
#include <Cocoa/Cocoa.h>
#include <CoreVideo/CoreVideo.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <mach-o/swap.h>
#include <bootstrap.h>

#ifdef __x86_64__
#include <emmintrin.h>
#elif __arm64__
#include <arm_neon.h>
#endif

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <assert.h>
#include <fcntl.h>
#include <regex.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <semaphore.h>
#include <pthread.h>
#include <pwd.h>
#include <spawn.h>
#include <libproc.h>

#include "misc/extern.h"
#include "misc/macros.h"
#include "misc/memory_pool.h"
#include "misc/ts.h"
//#include "misc/autorelease.h"
#include "misc/notify.h"
#include "misc/log.h"
#include "misc/helpers.h"
#include "misc/timer.h"
#ifdef YABAI_DEFINE_CORE
#define MACHO_DLSYM_IMPLEMENTATION
#endif
#include "misc/macho_dlsym.h"
#ifdef YABAI_DEFINE_CORE
#undef MACHO_DLSYM_IMPLEMENTATION
#endif
#include "misc/sbuffer.h"
#ifdef YABAI_DEFINE_CORE
#define HASHTABLE_IMPLEMENTATION
#endif
#include "misc/hashtable.h"
#ifdef YABAI_DEFINE_CORE
#undef HASHTABLE_IMPLEMENTATION
#endif
#include "misc/service.h"

#include "osax/common.h"

#include "spaces/view.h"
#include "sa/sa.h"
#include "events/event_loop.h"
#include "events/mission_control.h"
#include "events/event_signal.h"
#include "events/workspace.h"
#include "windows/rule.h"
#include "ipc/message.h"
#include "displays/display.h"
#include "spaces/space.h"
#include "windows/window.h"
#include "applications/process_manager.h"
#include "applications/application.h"
#include "displays/display_manager.h"
#include "spaces/space_manager.h"
#include "windows/window_manager.h"
#include "events/mouse_handler.h"

#include "effects/display.h"
#include "effects/window_fade.h"
#include "effects/snapshot.h"
#include "navigation/topology.h"
#include "navigation/admission.h"
#include "navigation/schedule.h"
#include "navigation/step.h"
#include "navigation/activation.h"
#include "navigation/command.h"
#include "hooks.h"

#pragma clang diagnostic pop
#endif
