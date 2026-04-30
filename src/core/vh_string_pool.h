#ifndef VH_STRING_POOL_H
#define VH_STRING_POOL_H

typedef struct VhStringPool {
    char *data;
    unsigned long size;
    unsigned long capacity;
    const char *tag;
} VhStringPool;

int vh_string_pool_init_tag(VhStringPool *pool, const char *tag);
int vh_string_pool_init(VhStringPool *pool);
void vh_string_pool_free(VhStringPool *pool);
int vh_string_pool_add(VhStringPool *pool, const char *s, unsigned long *out_off);
const char *vh_string_pool_get(const VhStringPool *pool, unsigned long off);
unsigned long vh_string_pool_bytes_used(const VhStringPool *pool);

#endif