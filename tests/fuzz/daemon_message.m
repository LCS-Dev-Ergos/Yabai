//
// libFuzzer target for the yabai daemon socket: request framing
// (daemon_message_read), the accept thread's navigation request check, the
// tokenizer and value parsers every command handler builds on, and the rule
// and signal handlers, which only parse and keep their entries. The other
// handlers act on live window-manager state and are not run; nor are rules
// that name a display or Space, which WindowServer resolves.
//

unsigned char __src_osax_payload[1];
unsigned int __src_osax_payload_len;
unsigned char __src_osax_loader[1];
unsigned int __src_osax_loader_len;

#include "../../src/manifest.m"

#include <fcntl.h>

// Rules and signals, patterns included: pattern.c refuses any whose
// compilation would cost more than a literal as long as a request. Rules that
// name a display or Space are left out, as WindowServer resolves those.
static bool fuzz_handler_input(char *message)
{
    struct token domain = get_token(&message);
    if (!token_equals(domain, DOMAIN_RULE) && !token_equals(domain, DOMAIN_SIGNAL)) return false;

    for (struct token token = get_token(&message); token.length > 0; token = get_token(&message)) {
        if (token_prefix(token, ARGUMENT_RULE_KEY_DISPLAY) || token_prefix(token, ARGUMENT_RULE_KEY_SPACE)) return false;
    }

    return true;
}

static void fuzz_handler_run(char *message)
{
    char *response = NULL;
    size_t response_size = 0;
    FILE *rsp = open_memstream(&response, &response_size);
    if (!rsp) return;

    struct token domain = get_token(&message);
    if (token_equals(domain, DOMAIN_RULE)) {
        handle_domain_rule(rsp, domain, message);
    } else {
        handle_domain_signal(rsp, domain, message);
    }

    fclose(rsp);
    free(response);

    for (int i = 0; i < buf_len(g_window_manager.rules); ++i) {
        rule_destroy(&g_window_manager.rules[i]);
    }
    buf_free(g_window_manager.rules);
    g_window_manager.rules = NULL;

    for (int type = SIGNAL_APPLICATION_LAUNCHED; type < SIGNAL_TYPE_COUNT; ++type) {
        for (int i = 0; i < buf_len(g_signal_event[type]); ++i) {
            event_signal_destroy(&g_signal_event[type][i]);
        }
        buf_free(g_signal_event[type]);
        g_signal_event[type] = NULL;
    }
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    if (!ts_init(MEGABYTES(8))) abort();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // The accept thread peeks at most 128 bytes of a request.
    space_navigation_request_direction((const char *) data, (int) (size < 128 ? size : 128));

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) abort();

    // A request larger than the socket buffer is truncated, as a slow client's would be.
    int buffer_size = 256 * 1024;
    setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
    setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    if (size) write(fds[0], data, size);
    shutdown(fds[0], SHUT_WR);

    const char *failure = NULL;
    char *message = daemon_message_read(fds[1], DAEMON_MESSAGE_TIMEOUT_MS, &failure);
    if (message) {
        // The handlers write into the request, so they get a copy.
        int length = 0;
        while (message[length] || message[length + 1]) ++length;
        char *copy = malloc(length + 2);
        if (copy) {
            memcpy(copy, message, length + 2);
            if (fuzz_handler_input(copy)) {
                memcpy(copy, message, length + 2);
                fuzz_handler_run(copy);
            }
            free(copy);
        }

        char *cursor = message;
        struct token token;
        do {
            token = get_token(&cursor);

            struct token_value value = token_to_value(token);
            (void) value;

            if (token.length > 0) {
                char *key, *text;
                bool exclusion = false;
                parse_key_value_pair(token.text, &key, &text, &exclusion);
            }
        } while (token.length > 0);
    }

    close(fds[0]);
    close(fds[1]);
    ts_reset();
    return 0;
}
