#include "vh_string_pool.h"
#include "vh_memtrack.h"

#include <stddef.h>
#include <string.h>

#define VH_STRING_POOL_INITIAL_CAPACITY 256UL

static int vh_string_pool_grow(VhStringPool *pool, unsigned long needed_size)
{
    unsigned long new_capacity;
    char *new_data;

    if (pool == NULL) {
        return 0;
    }

    if (pool->capacity >= needed_size) {
        return 1;
    }

    new_capacity = (pool->capacity == 0) ? VH_STRING_POOL_INITIAL_CAPACITY : pool->capacity;
    while (new_capacity < needed_size) {
        if (new_capacity > (~0UL / 2UL)) {
            new_capacity = needed_size;
            break;
        }
        new_capacity *= 2UL;
    }

    new_data = (char *)vh_realloc_tag(pool->data,
                                      (size_t)new_capacity,
                                      (pool->tag != NULL) ? pool->tag : "unknown");
    if (new_data == NULL) {
        return 0;
    }

    pool->data = new_data;
    pool->capacity = new_capacity;
    return 1;
}

static int vh_string_pool_find_existing(const VhStringPool *pool, const char *s, unsigned long *out_off)
{
    unsigned long pos;

    if (pool == NULL || s == NULL || out_off == NULL || pool->data == NULL || pool->size <= 1UL) {
        return 0;
    }

    pos = 1UL;
    while (pos < pool->size) {
        const char *existing = pool->data + pos;
        size_t existing_len = strlen(existing);

        if (strcmp(existing, s) == 0) {
            *out_off = pos;
            return 1;
        }

        pos += (unsigned long)existing_len + 1UL;
    }

    return 0;
}

int vh_string_pool_init_tag(VhStringPool *pool, const char *tag)
{
    if (pool == NULL) {
        return 0;
    }

    pool->data = NULL;
    pool->size = 0;
    pool->capacity = 0;
    pool->tag = (tag != NULL && tag[0] != '\0') ? tag : "unknown";

    if (!vh_string_pool_grow(pool, 2UL)) {
        return 0;
    }

    /* Reserve offset 0 as invalid/null to simplify caller logic. */
    pool->data[0] = '\0';
    pool->size = 1UL;
    return 1;
}

int vh_string_pool_init(VhStringPool *pool)
{
    return vh_string_pool_init_tag(pool, "unknown");
}

void vh_string_pool_free(VhStringPool *pool)
{
    if (pool == NULL) {
        return;
    }

    vh_free(pool->data);
    pool->data = NULL;
    pool->size = 0;
    pool->capacity = 0;
    pool->tag = NULL;
}

int vh_string_pool_add(VhStringPool *pool, const char *s, unsigned long *out_off)
{
    unsigned long off;
    unsigned long needed_size;
    size_t len;

    if (pool == NULL || s == NULL || out_off == NULL) {
        return 0;
    }

    if (vh_string_pool_find_existing(pool, s, &off)) {
        *out_off = off;
        return 1;
    }

    len = strlen(s);
    needed_size = pool->size + (unsigned long)len + 1UL;

    if (needed_size < pool->size) {
        return 0;
    }

    if (!vh_string_pool_grow(pool, needed_size)) {
        return 0;
    }

    off = pool->size;
    memcpy(pool->data + off, s, len + 1U);
    pool->size = needed_size;
    *out_off = off;
    return 1;
}

const char *vh_string_pool_get(const VhStringPool *pool, unsigned long off)
{
    if (pool == NULL || pool->data == NULL || off == 0UL || off >= pool->size) {
        return NULL;
    }

    return pool->data + off;
}

unsigned long vh_string_pool_bytes_used(const VhStringPool *pool)
{
    if (pool == NULL) {
        return 0UL;
    }

    return pool->size;
}