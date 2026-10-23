#include <slub.h>
#include <list.h>
#include <pmm.h>
#include <string.h>
#include <stdio.h>

// lab2 challenge 2411526: page-backed size-class object allocator.
#define SLUB_CLASSES 8
#define SLUB_MIN_SIZE 16
#define SLUB_MAGIC 0x51ab51abU

struct slab {
    unsigned int magic;
    unsigned int class_index;
    size_t page_count;
    size_t inuse;
    size_t capacity;
    void *free_object;
    list_entry_t link;
};

static list_entry_t partial[SLUB_CLASSES];

static uintptr_t slab_data_start(struct slab *slab) {
    return ROUNDUP((uintptr_t)(slab + 1), SLUB_MIN_SIZE);
}

static size_t class_size(unsigned int index) {
    return (size_t)SLUB_MIN_SIZE << index;
}

void slub_init(void) {
    for (unsigned int i = 0; i < SLUB_CLASSES; i++) {
        list_init(&partial[i]);
    }
}

void *slub_alloc(size_t size) {
    if (size == 0 || size > (size_t)-1 - PGSIZE) {
        return NULL;
    }
    unsigned int index = 0;
    while (index < SLUB_CLASSES && class_size(index) < size) {
        index++;
    }
    if (index == SLUB_CLASSES) {
        size_t count = (size + PGSIZE) / PGSIZE;
        struct Page *page = alloc_pages(count);
        if (page == NULL) {
            return NULL;
        }
        struct slab *slab = (struct slab *)(page2pa(page) + va_pa_offset);
        slab->magic = SLUB_MAGIC;
        slab->class_index = SLUB_CLASSES;
        slab->page_count = count;
        return (void *)slab_data_start(slab);
    }

    list_entry_t *head = &partial[index];
    struct slab *slab;
    if (list_empty(head)) {
        struct Page *page = alloc_page();
        if (page == NULL) {
            return NULL;
        }
        slab = (struct slab *)(page2pa(page) + va_pa_offset);
        slab->magic = SLUB_MAGIC;
        slab->class_index = index;
        slab->page_count = 1;
        slab->inuse = 0;
        size_t unit = class_size(index);
        slab->capacity = (PGSIZE - (slab_data_start(slab) - (uintptr_t)slab)) / unit;
        slab->free_object = NULL;
        for (size_t i = slab->capacity; i > 0; i--) {
            void *object = (void *)(slab_data_start(slab) + (i - 1) * unit);
            *(void **)object = slab->free_object;
            slab->free_object = object;
        }
        list_add(head, &slab->link);
    } else {
        slab = to_struct(list_next(head), struct slab, link);
    }
    void *object = slab->free_object;
    assert(object != NULL);
    slab->free_object = *(void **)object;
    slab->inuse++;
    if (slab->free_object == NULL) {
        list_del(&slab->link);
    }
    return object;
}

void slub_free(void *object) {
    if (object == NULL) {
        return;
    }
    struct slab *slab = (struct slab *)ROUNDDOWN((uintptr_t)object, PGSIZE);
    assert(slab->magic == SLUB_MAGIC);
    struct Page *page = pa2page(PADDR(slab));
    if (slab->class_index == SLUB_CLASSES) {
        assert((uintptr_t)object == slab_data_start(slab));
        size_t count = slab->page_count;
        slab->magic = 0;
        free_pages(page, count);
        return;
    }
    unsigned int index = slab->class_index;
    assert(index < SLUB_CLASSES && slab->inuse > 0);
    size_t unit = class_size(index);
    uintptr_t offset = (uintptr_t)object - slab_data_start(slab);
    assert((uintptr_t)object >= slab_data_start(slab) &&
           offset < slab->capacity * unit && offset % unit == 0);
    if (slab->free_object == NULL) {
        list_add(&partial[index], &slab->link);
    }
    *(void **)object = slab->free_object;
    slab->free_object = object;
    slab->inuse--;
    if (slab->inuse == 0) {
        list_del(&slab->link);
        slab->magic = 0;
        free_page(page);
    }
}

// lab2 challenge test 2411339: class boundaries, reuse, large objects, reclamation.
void slub_check(void) {
    size_t before = nr_free_pages();
    assert(slub_alloc(0) == NULL);
    void *many[260];
    for (size_t i = 0; i < 260; i++) {
        many[i] = slub_alloc(16);
        assert(many[i] != NULL);
    }
    void *small = slub_alloc(16);
    void *medium = slub_alloc(31);
    void *large_class = slub_alloc(2000);
    void *multi_page = slub_alloc(5000);
    assert(small != NULL && medium != NULL && large_class != NULL && multi_page != NULL);
    memset(small, 0x11, 16);
    memset(medium, 0x22, 31);
    memset(large_class, 0x33, 2000);
    memset(multi_page, 0x44, 5000);
    for (size_t i = 0; i < 16; i++) {
        assert(((unsigned char *)small)[i] == 0x11);
    }
    for (size_t i = 0; i < 31; i++) {
        assert(((unsigned char *)medium)[i] == 0x22);
    }
    for (size_t i = 0; i < 2000; i++) {
        assert(((unsigned char *)large_class)[i] == 0x33);
    }
    for (size_t i = 0; i < 5000; i++) {
        assert(((unsigned char *)multi_page)[i] == 0x44);
    }
    slub_free(medium);
    void *again = slub_alloc(31);
    assert(again == medium);
    slub_free(again);
    slub_free(small);
    slub_free(large_class);
    slub_free(multi_page);
    for (size_t i = 260; i > 0; i--) {
        slub_free(many[i - 1]);
    }
    assert(nr_free_pages() == before);
    cprintf("slub_check() succeeded!\n");
}
