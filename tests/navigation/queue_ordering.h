static void test_interleaved_groups(void)
{
    uint64_t now = 2000000000;

    assert(!space_navigation_queue_join(20, 1, now));
    assert(space_navigation_queue_join(21, 1, now + 30000000));

    // A query closes the first group. Repeats after it must still merge,
    // without changing the steps that the query will observe.
    assert(!space_navigation_queue_join(22, 0, now + 40000000));
    assert(!space_navigation_queue_join(23, 1, now + 60000000));
    assert(space_navigation_queue_join(24, 1, now + 90000000));
    assert(space_navigation_queue_join(25, -1, now + 100000000));

    assert(!space_navigation_queue_join(26, 0, now + 110000000));
    assert(!space_navigation_queue_join(27, -1, now + 120000000));
    assert(space_navigation_queue_join(28, -1, now + 150000000));

    space_navigation_queue_claim(20);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == 1);

    space_navigation_queue_claim(22);
    assert(!g_space_navigation_claim.active);

    space_navigation_queue_claim(23);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == 0);

    space_navigation_queue_claim(26);
    assert(!g_space_navigation_claim.active);

    space_navigation_queue_claim(27);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == -1);

    // Once a group has run, the same descriptor can identify a new one.
    assert(!space_navigation_queue_join(20, 1, now + 200000000));
    space_navigation_queue_claim(20);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == 1);
}

static void test_allocation_failure(void)
{
    uint64_t now = 3000000000;

    assert(!space_navigation_queue_join(30, 1, now));
    assert(!space_navigation_queue_join(31, 0, now + 10000000));

    // An allocation failure posts the request normally, preserving the
    // barrier between earlier groups and any later successful allocation.
    fail_allocation = true;
    assert(!space_navigation_queue_join(32, -1, now + 20000000));
    fail_allocation = false;

    assert(!space_navigation_queue_join(33, 1, now + 30000000));
    assert(space_navigation_queue_join(34, 1, now + 40000000));

    space_navigation_queue_claim(30);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == 1);

    space_navigation_queue_claim(31);
    assert(!g_space_navigation_claim.active);
    space_navigation_queue_claim(32);
    assert(!g_space_navigation_claim.active);

    space_navigation_queue_claim(33);
    assert(g_space_navigation_claim.active && g_space_navigation_claim.steps == 1);
    assert(!g_space_navigation_queue.first && !g_space_navigation_queue.last);
}
