#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/vh_string_pool.h"

static int expect_true(int condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", label);
        return 0;
    }
    return 1;
}

static int test_single_add(void)
{
    VhStringPool pool;
    unsigned long off;
    const char *value;
    int ok;

    ok = 1;
    ok = ok && expect_true(vh_string_pool_init(&pool), "init");
    ok = ok && expect_true(vh_string_pool_add(&pool, "AlienBreed", &off), "add one");
    value = vh_string_pool_get(&pool, off);
    ok = ok && expect_true(value != NULL, "get one");
    ok = ok && expect_true(strcmp(value, "AlienBreed") == 0, "value one");
    vh_string_pool_free(&pool);
    return ok;
}

static int test_multiple_and_dedup(void)
{
    VhStringPool pool;
    unsigned long off_a;
    unsigned long off_b;
    unsigned long off_a_again;
    int ok;

    ok = 1;
    ok = ok && expect_true(vh_string_pool_init(&pool), "init multi");
    ok = ok && expect_true(vh_string_pool_add(&pool, "A", &off_a), "add A");
    ok = ok && expect_true(vh_string_pool_add(&pool, "B", &off_b), "add B");
    ok = ok && expect_true(vh_string_pool_add(&pool, "A", &off_a_again), "add A again");
    ok = ok && expect_true(off_a != 0UL, "A offset nonzero");
    ok = ok && expect_true(off_b != 0UL, "B offset nonzero");
    ok = ok && expect_true(off_a == off_a_again, "dedup same offset");
    vh_string_pool_free(&pool);
    return ok;
}

static int test_realloc_stability(void)
{
    VhStringPool pool;
    unsigned long offsets[300];
    char inputs[300][48];
    int i;
    int ok;

    ok = 1;
    ok = ok && expect_true(vh_string_pool_init(&pool), "init realloc");

    for (i = 0; i < 300; ++i) {
        snprintf(inputs[i], sizeof(inputs[i]), "Game%03d_v1.0_AGA_En", i);
        ok = ok && expect_true(vh_string_pool_add(&pool, inputs[i], &offsets[i]), "add realloc item");
    }

    for (i = 0; i < 300; ++i) {
        const char *s = vh_string_pool_get(&pool, offsets[i]);
        ok = ok && expect_true(s != NULL, "get realloc item");
        ok = ok && expect_true(strcmp(s, inputs[i]) == 0, "realloc value stable");
    }

    vh_string_pool_free(&pool);
    return ok;
}

static int test_empty_and_invalid_offsets(void)
{
    VhStringPool pool;
    unsigned long off_empty;
    unsigned long off_empty_2;
    const char *s;
    int ok;

    ok = 1;
    ok = ok && expect_true(vh_string_pool_init(&pool), "init empty");
    ok = ok && expect_true(vh_string_pool_add(&pool, "", &off_empty), "add empty");
    ok = ok && expect_true(vh_string_pool_add(&pool, "", &off_empty_2), "add empty again");
    ok = ok && expect_true(off_empty == off_empty_2, "empty dedup");

    s = vh_string_pool_get(&pool, off_empty);
    ok = ok && expect_true(s != NULL, "get empty");
    ok = ok && expect_true(strcmp(s, "") == 0, "empty value");

    ok = ok && expect_true(vh_string_pool_get(&pool, 0UL) == NULL, "offset zero invalid");
    ok = ok && expect_true(vh_string_pool_get(&pool, vh_string_pool_bytes_used(&pool) + 10UL) == NULL, "offset past end invalid");

    vh_string_pool_free(&pool);
    return ok;
}

int main(void)
{
    int ok;

    ok = 1;
    ok = ok && test_single_add();
    ok = ok && test_multiple_and_dedup();
    ok = ok && test_realloc_stability();
    ok = ok && test_empty_and_invalid_offsets();

    if (!ok) {
        return 1;
    }

    printf("vh_string_pool tests passed\n");
    return 0;
}
