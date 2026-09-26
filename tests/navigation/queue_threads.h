#include <sched.h>

static struct
{
    pthread_mutex_t lock;
    pthread_cond_t ready;
    int descriptors[4000];
    bool navigation[4000];
    int count;
    bool done;
} posted = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .ready = PTHREAD_COND_INITIALIZER
};

static void *claim_posted(void *unused)
{
    int index = 0;

    for (;;) {
        pthread_mutex_lock(&posted.lock);

        while (index == posted.count && !posted.done) {
            pthread_cond_wait(&posted.ready, &posted.lock);
        }

        if (index == posted.count && posted.done) {
            pthread_mutex_unlock(&posted.lock);
            break;
        }

        int descriptor = posted.descriptors[index];
        bool navigation = posted.navigation[index++];
        pthread_mutex_unlock(&posted.lock);

        space_navigation_queue_claim(descriptor);
        assert(g_space_navigation_claim.active == navigation);
        if (navigation) assert(g_space_navigation_claim.steps == 1);

        sched_yield();
    }

    return NULL;
}

static void test_concurrent_groups(void)
{
    pthread_t consumer;
    assert(pthread_create(&consumer, NULL, claim_posted, NULL) == 0);

    for (int i = 0; i < 4000; ++i) {
        int direction = i % 10 == 9 ? 0 : 1;
        uint64_t now = 4000000000ULL + i * 30000000ULL;
        if (space_navigation_queue_join(i + 100, direction, now)) continue;

        pthread_mutex_lock(&posted.lock);
        posted.descriptors[posted.count] = i + 100;
        posted.navigation[posted.count++] = direction != 0;
        pthread_cond_signal(&posted.ready);
        pthread_mutex_unlock(&posted.lock);

        sched_yield();
    }

    pthread_mutex_lock(&posted.lock);
    posted.done = true;
    pthread_cond_signal(&posted.ready);
    pthread_mutex_unlock(&posted.lock);

    assert(pthread_join(consumer, NULL) == 0);
    assert(!g_space_navigation_queue.first && !g_space_navigation_queue.last);
}
