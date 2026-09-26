//
// libFuzzer target for the yabai daemon socket: request framing
// (daemon_message_read) and the tokenizer and value parsers every command
// handler builds on. The handlers themselves act on live window-manager state
// and are not run.
//

unsigned char __src_osax_payload[1];
unsigned int __src_osax_payload_len;
unsigned char __src_osax_loader[1];
unsigned int __src_osax_loader_len;

#include "../../src/manifest.m"

#include <fcntl.h>

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    if (!ts_init(MEGABYTES(8))) abort();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) abort();

    // A request larger than the socket buffer is truncated, as a slow client's would be.
    int buffer_size = 256 * 1024;
    setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
    setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    if (size) write(fds[0], data, size);
    shutdown(fds[0], SHUT_WR);

    char *message = daemon_message_read(fds[1]);
    if (message) {
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
