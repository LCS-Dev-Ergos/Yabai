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
// step, so pressing a key three times still moves three spaces.

#define SPACE_NAVIGATION_REPEAT_NS 50000000ULL

static struct
{
    pthread_mutex_t lock;
    int owner;
    int steps;
    int direction;
    bool open;
    uint64_t time;
} g_space_navigation_queue = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .owner = -1
};

// The steps the request being handled on the event loop carries.
static struct
{
    bool active;
    int steps;
} g_space_navigation_claim;

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

// Returns 1 for `space --navigate focus next <opacity> <duration>`, -1 for
// prev, and 0 for anything else, including an incomplete message.
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
    if (!space_navigation_request_number(token[4], false) || !space_navigation_request_number(token[5], true)) return 0;

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

    if (!direction) {
        g_space_navigation_queue.open = false;
    } else if (g_space_navigation_queue.owner != -1 && g_space_navigation_queue.open) {
        bool repeat = direction == g_space_navigation_queue.direction
                   && now - g_space_navigation_queue.time < SPACE_NAVIGATION_REPEAT_NS;

        if (!repeat) g_space_navigation_queue.steps += direction;

        g_space_navigation_queue.direction = direction;
        g_space_navigation_queue.time = now;
        joined = true;
    } else if (g_space_navigation_queue.owner == -1) {
        g_space_navigation_queue.owner = sockfd;
        g_space_navigation_queue.steps = direction;
        g_space_navigation_queue.direction = direction;
        g_space_navigation_queue.open = true;
        g_space_navigation_queue.time = now;
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

    g_space_navigation_claim.active = g_space_navigation_queue.owner == sockfd;
    g_space_navigation_claim.steps = g_space_navigation_queue.steps;

    if (g_space_navigation_claim.active) {
        g_space_navigation_queue.owner = -1;
        g_space_navigation_queue.open = false;
    }

    pthread_mutex_unlock(&g_space_navigation_queue.lock);
}
