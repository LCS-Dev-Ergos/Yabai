#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

struct memory_pool
{
    void *memory;
    uint64_t size;
    volatile uint64_t used;
};

bool memory_pool_init(struct memory_pool *pool, uint64_t size)
{
    int page_size = getpagesize();

    uint64_t num_pages = size / page_size;
    uint64_t remainder = size % page_size;
    if (remainder) num_pages++;

    pool->used = 0;
    pool->size = num_pages * page_size;
    pool->memory = mmap(0, pool->size + page_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

    bool result = pool->memory != MAP_FAILED;
    if (result) mprotect(pool->memory + pool->size, page_size, PROT_NONE);

    return result;
}

#endif
