#include <assert.h>
#include <error.h>
#include <pmm.h>
#include <proc.h>
#include <stdio.h>
#include <string.h>

/* Optional stress tests, separate from the unaltered course assertions. */
void test_lab4_edges(void)
{
    size_t before = nr_free_pages();
    struct Page *root_page = alloc_page(), *target = alloc_page();
    assert(root_page != NULL && target != NULL);
    pde_t *root = page2kva(root_page);
    memset(root, 0, PGSIZE);
    list_entry_t held;
    list_init(&held);
    struct Page *p;
    while ((p = alloc_page()) != NULL) list_add(&held, &p->page_link);
    uintptr_t va = (uintptr_t)1 << 30;
    assert(page_insert(root, target, va, PTE_R) == -E_NO_MEM);
    assert(root[PDX1(va)] == 0 && page_ref(target) == 0);
    assert(nr_free_pages() == 0);
    list_entry_t *le = list_next(&held);
    p = le2page(le, page_link);
    list_del(le);
    free_page(p);
    // One free page: first-level allocation succeeds, second one must fail.
    assert(page_insert(root, target, va, PTE_R) == -E_NO_MEM);
    assert(root[PDX1(va)] == 0 && nr_free_pages() == 1);
    assert(page_ref(target) == 0);
    page_remove(root, va);
    while (!list_empty(&held)) {
        le = list_next(&held);
        p = le2page(le, page_link);
        list_del(le);
        free_page(p);
    }
    free_page(target);
    free_page(root_page);
    assert(nr_free_pages() == before);
    cprintf("[Edge PMM OOM rollback] PASS\n");
}

static int never_started(void *arg) { return 0; }

void test_lab4_fork_oom(void)
{
    size_t before = nr_free_pages();
    list_entry_t held;
    list_init(&held);
    struct Page *p;
    while ((p = alloc_page()) != NULL) list_add(&held, &p->page_link);
    struct proc_struct *parent = current;
    assert(kernel_thread(never_started, NULL, 0) == -E_NO_MEM);
    assert(current == parent && nr_free_pages() == 0);
    while (!list_empty(&held)) {
        list_entry_t *le = list_next(&held);
        p = le2page(le, page_link);
        list_del(le);
        free_page(p);
    }
    assert(nr_free_pages() == before);
    cprintf("[Edge fork OOM cleanup] PASS\n");
}
