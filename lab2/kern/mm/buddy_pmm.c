#include <buddy_pmm.h>
#include <list.h>
#include <stdio.h>

// lab2 function2-3 2411339: power-of-two page blocks with buddy coalescing.
#define BUDDY_ORDERS 20

static list_entry_t free_lists[BUDDY_ORDERS];
static size_t free_count;

static size_t block_pages(unsigned int order) {
    return (size_t)1 << order;
}

static unsigned int order_for(size_t n) {
    unsigned int order = 0;
    while (order < BUDDY_ORDERS && block_pages(order) < n) {
        order++;
    }
    return order;
}

static void insert_block(struct Page *base, unsigned int order) {
    base->property = block_pages(order);
    SetPageProperty(base);
    list_add(&free_lists[order], &base->page_link);
}

static void buddy_init(void) {
    for (unsigned int i = 0; i < BUDDY_ORDERS; i++) {
        list_init(&free_lists[i]);
    }
    free_count = 0;
}

static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    for (struct Page *p = base; p < base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }
    size_t index = base - pages;
    while (n > 0) {
        unsigned int order = 0;
        while (order + 1 < BUDDY_ORDERS && block_pages(order + 1) <= n &&
               (index & (block_pages(order + 1) - 1)) == 0) {
            order++;
        }
        insert_block(pages + index, order);
        size_t count = block_pages(order);
        index += count;
        n -= count;
        free_count += count;
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    if (n == 0) {
        return NULL;
    }
    unsigned int wanted = order_for(n);
    if (wanted == BUDDY_ORDERS || block_pages(wanted) > free_count) {
        return NULL;
    }
    unsigned int order = wanted;
    while (order < BUDDY_ORDERS && list_empty(&free_lists[order])) {
        order++;
    }
    if (order == BUDDY_ORDERS) {
        return NULL;
    }
    struct Page *base = le2page(list_next(&free_lists[order]), page_link);
    list_del(&base->page_link);
    ClearPageProperty(base);
    base->property = 0;
    while (order > wanted) {
        order--;
        insert_block(base + block_pages(order), order);
    }
    free_count -= block_pages(wanted);
    return base;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    unsigned int order = order_for(n);
    assert(order < BUDDY_ORDERS);
    size_t count = block_pages(order);
    size_t index = base - pages;
    assert((index & (count - 1)) == 0 && index + count <= npage - nbase);
    for (struct Page *p = base; p < base + count; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->property = 0;
        set_page_ref(p, 0);
    }
    free_count += count;
    while (order + 1 < BUDDY_ORDERS) {
        size_t other = index ^ count;
        if (other + count > npage - nbase) {
            break;
        }
        struct Page *peer = pages + other;
        if (!PageProperty(peer) || peer->property != count) {
            break;
        }
        list_del(&peer->page_link);
        ClearPageProperty(peer);
        peer->property = 0;
        if (other < index) {
            index = other;
        }
        order++;
        count <<= 1;
    }
    insert_block(pages + index, order);
}

static size_t buddy_nr_free_pages(void) {
    return free_count;
}

static int buddy_free_block_covers(struct Page *first, struct Page *last,
                                   unsigned int min_order) {
    if (first > last) {
        struct Page *tmp = first;
        first = last;
        last = tmp;
    }
    for (unsigned int order = min_order; order < BUDDY_ORDERS; order++) {
        list_entry_t *head = &free_lists[order];
        for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
            struct Page *block = le2page(le, page_link);
            if (block <= first && last < block + block_pages(order)) {
                return 1;
            }
        }
    }
    return 0;
}

// lab2 test2-3 2411526: split, round up, merge, failure, and count invariants.
static void buddy_check(void) {
    size_t before = free_count;
    assert(buddy_alloc_pages(0) == NULL);
    assert(buddy_alloc_pages(before + 1) == NULL);

    struct Page *a = buddy_alloc_pages(1);
    struct Page *b = buddy_alloc_pages(1);
    assert(a != NULL && b != NULL && a != b);
    assert(free_count == before - 2);
    buddy_free_pages(a, 1);
    buddy_free_pages(b, 1);
    assert(free_count == before);
    if (((size_t)(a - pages) ^ 1) == (size_t)(b - pages)) {
        assert(buddy_free_block_covers(a, b, 1));
    }
    struct Page *pair = buddy_alloc_pages(2);
    assert(pair != NULL && free_count == before - 2);
    buddy_free_pages(pair, 2);

    struct Page *three = buddy_alloc_pages(3);
    assert(three != NULL && free_count == before - 4);
    buddy_free_pages(three, 3);
    assert(free_count == before);
    struct Page *four = buddy_alloc_pages(4);
    assert(four != NULL && free_count == before - 4);
    buddy_free_pages(four, 2);
    assert(free_count == before - 2);
    buddy_free_pages(four + 2, 2);
    assert(free_count == before);
    assert(buddy_free_block_covers(four, four + 3, 2));
    cprintf("buddy_check() succeeded!\n");
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
