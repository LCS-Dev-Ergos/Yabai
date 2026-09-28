// Fork: coalesces relative space navigation requests.
//
// A navigation waits on WindowServer and applications for tens of
// milliseconds, while a held key repeats every 30 ms. Queuing every request
// would keep switching long after the key is released. The accept thread
// therefore lets a `space --navigate focus next|prev` request join the one
// already waiting in the event queue, and answers it at once.
//
// A key repeat, which arrives less than SPACE_NAVIGATION_REPEAT_NS after the
// previous request, only keeps the pending step. A separate key press adds a
// step, so pressing a key three times still moves three spaces. A group that
// a repeat starts is marked, so that the navigation schedule, which queues
// the steps of groups already handled, keeps at most one of them pending too.
//
// Each request reaches us through skhd, a shell and the yabai client, whose
// start varies with load: during a switch, repeats sent 30 ms apart arrived
// more than 50 ms apart, counted as presses, and a held key went on for two
// Desktops after its release. Presses a person makes in a row are at least
// about 100 ms apart, so the bound sits between the two.

#define SPACE_NAVIGATION_REPEAT_NS 75000000ULL

struct space_navigation_group
{
    struct space_navigation_group *next;
    int owner;
    int steps;
    int direction;
    uint64_t time;
    bool repeat;
};

static struct
{
    pthread_mutex_t lock;
    struct space_navigation_group *first;
    struct space_navigation_group *last;
    bool open;

    // The last relative request, grouped or not.
    int direction;
    uint64_t time;
} g_space_navigation_queue = {
    .lock = PTHREAD_MUTEX_INITIALIZER
};

static struct space_navigation_claim g_space_navigation_claim;

static bool space_navigation_request_token(const char **cursor, const char *end, const char **token)
{
    const char *terminator = memchr(*cursor, '\0', end - *cursor);
    if (!terminator) return false;

    *token = *cursor;
    *cursor = terminator + 1;

    return true;
}

static bool space_navigation_request_number(const char *token, bool allow_zero)
{
    char *number_end = NULL;
    float value = strtof(token, &number_end);

    if (number_end == token || *number_end != '\0' || !isfinite(value)) return false;

    return value <= 1.0f && (allow_zero ? value >= 0.0f : value > 0.0f);
}

// Returns 1 for `space --navigate focus next <effect> <duration>`, where the
// effect is crossfade or a starting opacity, -1 for prev, and 0 for anything
// else, including an incomplete message.
static int space_navigation_request_direction(const char *bytes, int length)
{
    int size;
    if (length < (int) sizeof(size)) return 0;

    memcpy(&size, bytes, sizeof(size));
    if (size <= 0 || size != length - (int) sizeof(size)) return 0;

    const char *cursor = bytes + sizeof(size);
    const char *end = cursor + size;
    const char *token[7];

    for (int i = 0; i < 7; ++i) {
        if (!space_navigation_request_token(&cursor, end, &token[i])) return 0;
    }

    if (cursor != end || *token[6] != '\0') return 0;
    if (strcmp(token[0], "space") != 0 || strcmp(token[1], "--navigate") != 0 || strcmp(token[2], "focus") != 0) return 0;
    if (strcmp(token[4], "crossfade") != 0 && !space_navigation_request_number(token[4], false)) return 0;
    if (!space_navigation_request_number(token[5], true)) return 0;

    if (strcmp(token[3], "next") == 0) return 1;
    if (strcmp(token[3], "prev") == 0) return -1;

    return 0;
}

// Accept thread. Returns true when the request joined the waiting one, and
// must be answered instead of posted. Any other request closes the group, so
// requests still run in the order they arrived.
static bool space_navigation_queue_join(int sockfd, int direction, uint64_t now)
{
    bool joined = false;

    pthread_mutex_lock(&g_space_navigation_queue.lock);

    struct space_navigation_group *last = g_space_navigation_queue.last;
    bool repeat = direction && direction == g_space_navigation_queue.direction
               && now - g_space_navigation_queue.time < SPACE_NAVIGATION_REPEAT_NS;

    if (direction) {
        g_space_navigation_queue.direction = direction;
        g_space_navigation_queue.time = now;
    }

    if (!direction) {
        g_space_navigation_queue.open = false;
    } else if (last && g_space_navigation_queue.open) {
        if (!repeat) last->steps += direction;

        last->direction = direction;
        last->time = now;
        joined = true;
    } else {
        struct space_navigation_group *group = malloc(sizeof(*group));
        g_space_navigation_queue.open = group != NULL;

        if (group) {
            *group = (struct space_navigation_group) {
                .owner = sockfd,
                .steps = direction,
                .direction = direction,
                .time = now,
                .repeat = repeat
            };

            if (last) last->next = group;
            else g_space_navigation_queue.first = group;

            g_space_navigation_queue.last = group;
        }
    }

    pthread_mutex_unlock(&g_space_navigation_queue.lock);

    return joined;
}

// Event loop, before each daemon message is read. The waiting request takes
// the steps of its group; its socket stays open until then, so no other
// connection can reuse the descriptor.
static void space_navigation_queue_claim(int sockfd)
{
    pthread_mutex_lock(&g_space_navigation_queue.lock);

    struct space_navigation_group *first = g_space_navigation_queue.first;
    g_space_navigation_claim.active = first && first->owner == sockfd;
    g_space_navigation_claim.steps  = g_space_navigation_claim.active ? first->steps : 0;
    g_space_navigation_claim.repeat = g_space_navigation_claim.active && first->repeat;

    if (g_space_navigation_claim.active) {
        g_space_navigation_queue.first = first->next;

        if (g_space_navigation_queue.last == first) {
            g_space_navigation_queue.last = NULL;
            g_space_navigation_queue.open = false;
        }

        free(first);
    }

    pthread_mutex_unlock(&g_space_navigation_queue.lock);
}

// Event loop, while the claimed request is handled.
static struct space_navigation_claim space_navigation_queue_claimed(void)
{
    return g_space_navigation_claim;
}

// Accept thread. The client sends its whole request right after connecting;
// a request that is not readable almost at once is posted as usual.
static bool space_navigation_accept(int sockfd)
{
    char bytes[128];
    int direction = 0;
    struct pollfd readable = { .fd = sockfd, .events = POLLIN };

    if (poll(&readable, 1, 10) == 1) {
        ssize_t length = recv(sockfd, bytes, sizeof(bytes), MSG_PEEK);
        if (length > 0) direction = space_navigation_request_direction(bytes, (int) length);
    }

    if (!space_navigation_queue_join(sockfd, direction, read_os_timer())) return false;

    while (recv(sockfd, bytes, sizeof(bytes), MSG_DONTWAIT) > 0);
    socket_close(sockfd);

    return true;
}
