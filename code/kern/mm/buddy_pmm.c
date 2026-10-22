#include <pmm.h>
#include <list.h>
#include <string.h>
#include <stdio.h>
#include <buddy_pmm.h>
#include <assert.h>

#define MAX_ORDER 11

static free_area_t free_area[MAX_ORDER + 1];

#define free_list(order) (free_area[order].free_list)
#define nr_free(order) (free_area[order].nr_free)

static void buddy_init(void) {
    for (int i = 0; i <= MAX_ORDER; i++) {
        list_init(&free_list(i));
        nr_free(i) = 0;
    }
}

static struct Page *buddy_page(struct Page *base, int order) {
    size_t base_idx = base - pages;
    size_t buddy_idx = base_idx ^ (1 << order);
    if (buddy_idx >= npage) return NULL;
    return &pages[buddy_idx];
}

static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }

    size_t remaining = n;
    struct Page *curr = base;
    while (remaining > 0) {
        int order = 0;
        while ((1 << (order + 1)) <= remaining && ((curr - pages) & (1 << order)) == 0 && order < MAX_ORDER) {
            order++;
        }
        
        curr->property = order;
        SetPageProperty(curr);
        nr_free(order) ++;
        
        list_entry_t *le = &free_list(order);
        while ((le = list_next(le)) != &free_list(order)) {
            if (curr < le2page(le, page_link)) {
                list_add_before(le, &(curr->page_link));
                break;
            }
        }
        if (le == &free_list(order)) {
            list_add(&free_list(order), &(curr->page_link));
        }

        remaining -= (1 << order);
        curr += (1 << order);
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);
    int order = 0;
    while ((1 << order) < n && order < MAX_ORDER) {
        order++;
    }
    if ((1 << order) < n) return NULL; 

    for (int i = order; i <= MAX_ORDER; i++) {
        if (!list_empty(&free_list(i))) {
            list_entry_t *le = list_next(&free_list(i));
            struct Page *page = le2page(le, page_link);
            list_del(le);
            nr_free(i) --;
            
            while (i > order) {
                i--;
                struct Page *buddy = page + (1 << i);
                buddy->property = i;
                SetPageProperty(buddy);
                list_add(&free_list(i), &(buddy->page_link));
                nr_free(i) ++;
            }
            
            ClearPageProperty(page);
            return page;
        }
    }
    return NULL;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    int order = 0;
    while ((1 << order) < n && order < MAX_ORDER) {
        order++;
    }
    
    struct Page *p = base;
    for (; p != base + (1 << order); p++) {
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = order;
    SetPageProperty(base);
    nr_free(order) ++;
    
    list_add(&free_list(order), &(base->page_link));
    
    while (order < MAX_ORDER) {
        struct Page *buddy = buddy_page(base, order);
        if (buddy == NULL || !PageProperty(buddy) || buddy->property != order) {
            break;
        }
        
        list_del(&(buddy->page_link));
        nr_free(order) --;
        
        if (base > buddy) {
            struct Page *temp = base;
            base = buddy;
            buddy = temp;
        }
        
        ClearPageProperty(buddy);
        order++;
        base->property = order;
        nr_free(order) ++;
    }
}

static size_t buddy_nr_free_pages(void) {
    size_t total = 0;
    for (int i = 0; i <= MAX_ORDER; i++) {
        total += nr_free(i) * (1 << i);
    }
    return total;
}

// 测试函数
static void buddy_check(void) {
    int count = 0, total = 0;
    for (int order = 0; order <= MAX_ORDER; order++) {
        list_entry_t *le = &free_list(order);
        while ((le = list_next(le)) != &free_list(order)) {
            struct Page *p = le2page(le, page_link);
            assert(PageProperty(p));
            count ++, total += (1 << p->property);
        }
    }
    assert(total == buddy_nr_free_pages());

    struct Page *p0 = alloc_pages(1);
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    struct Page *p1 = alloc_pages(2);
    assert(p1 != NULL);
    assert(!PageProperty(p1));

    free_pages(p0, 1);
    free_pages(p1, 2);
    
    assert(total == buddy_nr_free_pages());
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