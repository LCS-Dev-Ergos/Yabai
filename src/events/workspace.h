#ifndef WORKSPACE_H
#define WORKSPACE_H

#include "../core_types.h"

// Area: workspace.m observes NSWorkspace, Dock and display notifications.
// Thread: the main run loop receives callbacks and posts daemon events.
// State: g_workspace_context and macOS-version flags, set at startup.
// Callers: startup, process management and application observation.
// Calls: NSWorkspace, process helpers and event_loop_post.

#define SUPPORTED_MACOS_VERSION_LIST    \
    SUPPORT_MACOS_VERSION(goldengate, 27) \
    SUPPORT_MACOS_VERSION(tahoe,      26) \
    SUPPORT_MACOS_VERSION(sequoia,    15) \
    SUPPORT_MACOS_VERSION(sonoma,     14) \
    SUPPORT_MACOS_VERSION(ventura,    13) \
    SUPPORT_MACOS_VERSION(monterey,   12) \
    SUPPORT_MACOS_VERSION(bigsur,     11)

#define SUPPORT_MACOS_VERSION(name, major_version) \
static bool _workspace_is_macos_version_##name; \
static inline bool workspace_is_macos_##name(void) \
{ \
    return _workspace_is_macos_version_##name; \
}
    SUPPORTED_MACOS_VERSION_LIST
#undef SUPPORT_MACOS_VERSION

@interface workspace_context : NSObject {
}
- (id)init;
@end

struct process;
void *workspace_application_create_running_ns_application(struct process *process);
void workspace_application_destroy_running_ns_application(void *context, struct process *process);
void workspace_application_unobserve(void *context, struct process *process);
bool workspace_application_is_observable(struct process *process);
bool workspace_application_is_finished_launching(struct process *process);
void workspace_application_observe_finished_launching(void *context, struct process *process);
void workspace_application_observe_activation_policy(void *context, struct process *process);
int workspace_display_notch_height(uint32_t did);
pid_t workspace_get_dock_pid(void);
bool workspace_event_handler_begin(void **context);
bool workspace_use_macos_space_workaround(void);

extern void *g_workspace_context;

#endif
