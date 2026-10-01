unsigned char __src_osax_payload[1];
unsigned int __src_osax_payload_len;
unsigned char __src_osax_loader[1];
unsigned int __src_osax_loader_len;

#define YABAI_TEST_VIEW_GEOMETRY
#define YABAI_TEST_DIRECTION_WINDOWS
#define YABAI_TEST_RULE_EFFECTS
#define YABAI_TEST_COMMAND_STATE
#define YABAI_TEST_RESOURCE_LIFETIME
#define YABAI_TEST_AX_OBSERVATION
#import <ApplicationServices/ApplicationServices.h>
static AXError test_ax_set_messaging_timeout(AXUIElementRef element, float timeout);
static AXError test_ax_observer_create(pid_t pid, AXObserverCallback callback, AXObserverRef *observer);
static AXError test_ax_observer_add_notification(AXObserverRef observer, AXUIElementRef element, CFStringRef notification, void *context);
static AXError test_ax_observer_remove_notification(AXObserverRef observer, AXUIElementRef element, CFStringRef notification);
static CFRunLoopSourceRef test_ax_observer_get_run_loop_source(AXObserverRef observer);
struct process;
static void test_ax_retry_dispatch(uint64_t delay, dispatch_block_t block);
static struct process *test_ax_retry_find(ProcessSerialNumber *psn);
static void test_ax_retry_post(struct process *process);
#include "../../src/manifest.m"

static struct window test_command_window = { .id = 501 };
static uint32_t test_command_active_display_id(void)
{
    return 7;
}

static uint64_t test_command_active_space(void)
{
    return 42;
}

static struct window *test_command_focused_window(struct window_manager *wm)
{
    (void)wm;
    return &test_command_window;
}

static uint32_t test_direction_window_order[8];
static int test_direction_window_count;
static uint32_t *test_direction_space_window_list(uint64_t sid, int *count, bool include_minimized)
{
    (void)sid;
    (void)include_minimized;
    *count = test_direction_window_count;
    return test_direction_window_count ? test_direction_window_order : NULL;
}

static int test_rule_floating_calls;
static int test_rule_sticky_calls;
static bool test_rule_last_float;
static bool test_rule_last_sticky;
static void test_rule_make_window_floating(struct space_manager *sm, struct window_manager *wm, struct window *window, bool should_float, bool force)
{
    (void)sm;
    (void)wm;
    (void)window;
    (void)force;
    test_rule_floating_calls++;
    test_rule_last_float = should_float;
    if (should_float) {
        window_set_flag(window, WINDOW_FLOAT);
    } else {
        window_clear_flag(window, WINDOW_FLOAT);
    }
}

static void test_rule_make_window_sticky(struct space_manager *sm, struct window_manager *wm, struct window *window, bool should_sticky)
{
    (void)sm;
    (void)wm;
    (void)window;
    test_rule_sticky_calls++;
    test_rule_last_sticky = should_sticky;
}

static uint32_t test_view_space_display_id(uint64_t sid)
{
    (void)sid;
    return 1;
}

static CGRect test_view_display_bounds_constrained(uint32_t did, bool ignore_external_bar)
{
    (void)did;
    (void)ignore_external_bar;
    return CGRectMake(0, 0, 1000, 600);
}

static bool test_view_space_is_visible(uint64_t sid)
{
    (void)sid;
    return false;
}

#define TEST_SIG(name) bool test_##name(void)
typedef TEST_SIG(function);

#define TEST_FUNC(name, ...) static TEST_SIG(name) { char *test_name = #name; bool result = true; __VA_ARGS__ return result; }
#define TEST_CHECK(r, e) if ((r) != (e)) { printf("                   \e[1;33m%s\e[m\e[1;31m#%d %s == %s\e[m \e[1;31m(%d == %d)\e[m\n", test_name, __LINE__, #r, #e, r, e); result = false; }

#include "area.c"
#include "view_tree.c"
#include "view_layout.c"
#include "window_rules.c"
#include "command_domains.c"
#include "navigation_args.c"
#include "signal_dispatch.c"
#include "signal_environment.c"
#include "opacity_policy.c"
#include "string_escape.c"
#include "daemon_message_io.c"
#include "sa_request.c"
#include "resource_lifetime.c"
#include "application_observation.c"

#define TEST_ENTRY(name) { #name, test_##name },
#define TEST_LIST                                              \
    TEST_ENTRY(application_observation)                        \
    TEST_ENTRY(opacity_policy)                                 \
    TEST_ENTRY(string_escape)                                  \
    TEST_ENTRY(daemon_message_bounded_read)                    \
    TEST_ENTRY(daemon_message_bounded_reply)                   \
    TEST_ENTRY(daemon_message_round_trip)                      \
    TEST_ENTRY(signal_socket_lifetime)                         \
    TEST_ENTRY(signal_storage_bound)                           \
    TEST_ENTRY(sa_request_bounds)                              \
    TEST_ENTRY(signal_environment)                             \
    TEST_ENTRY(signal_standard_output)                         \
    TEST_ENTRY(signal_responsibility)                          \
    TEST_ENTRY(navigation_numbers)                             \
    TEST_ENTRY(display_area_is_in_direction)                   \
    TEST_ENTRY(closest_display_in_direction)                  \
    TEST_ENTRY(view_tree_insert_and_remove)                   \
    TEST_ENTRY(view_tree_split_preferences)                   \
    TEST_ENTRY(view_tree_rotation_and_mirror)                 \
    TEST_ENTRY(view_tree_balance_and_equalize)                \
    TEST_ENTRY(view_tree_fence_and_traversal)                  \
    TEST_ENTRY(view_layout_padding_and_gap_commands)          \
    TEST_ENTRY(view_layout_tree_commands)                      \
    TEST_ENTRY(view_layout_type_command)                       \
    TEST_ENTRY(window_rule_matching_and_effects)               \
    TEST_ENTRY(window_tree_lookup_and_direction)              \
    TEST_ENTRY(command_domain_errors)                          \
    TEST_ENTRY(command_domain_state_changes)                  \
    TEST_ENTRY(config_navigation_fade_curve)                  \
    TEST_ENTRY(config_navigation_veil_blur)                   \
    TEST_ENTRY(jankyborders_resource_lifetime)                \
    TEST_ENTRY(mouse_tap_resource_lifetime)

static struct {
    char *name;
    test_function *func;
} tests[] = {
    TEST_LIST
};

int main(int argc, char **argv)
{
    int succeeded = 0;
    int failed = 0;
    int total = 0;
    for (int i = 0; i < array_count(tests); ++i) {
        if (argc == 1 || strcmp(argv[1], tests[i].name) == 0) ++total;
    }
    if (total == 0) return EXIT_FAILURE;

    printf("\e[1;34m -- Running %d tests\e[m\n\n", total);

    uint64_t cpu_freq  = read_cpu_freq();
    uint64_t begin_tsc = read_cpu_timer();

    for (int i = 0; i < array_count(tests); ++i) {
        if (argc > 1 && strcmp(argv[1], tests[i].name) != 0) continue;
        uint64_t tsc = read_cpu_timer();
        bool result = tests[i].func();
        double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - tsc) / (double)cpu_freq;

        printf("(%0.4fms) %s \e[1;33m%s\e[m\n", ms_elapsed, result ? "\e[1;32msuccess\e[m" : " \e[1;31mfailed\e[m", tests[i].name);
        if (result) ++succeeded; else ++failed;
    }

    double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - begin_tsc) / (double)cpu_freq;
    printf("\n\e[1;34m -- Completed (%0.4fms)\e[m\n", ms_elapsed);
    printf("\t%d \e[1;32msucceeded\e[m\n", succeeded);
    printf("\t%d \e[1;31mfailed\e[m\n", failed);
    printf("\t%d \e[1;33mtotal\e[m\n", total);

    return total == succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
