#include <pmm.h>
#include <list.h>
#include <best_fit_pmm.h>
#include <stdio.h>

//lab2 function2-2 2412934
/* *
 * Best-Fit 物理内存管理器
 *
 * 与 First-Fit 的唯一本质差别在于链表的排序键：
 *   First-Fit : free_list 按块首地址(page)升序          -> 第一个 property >= n 的块即"首次命中"
 *   Best-Fit  : free_list 按块大小(property)升序        -> 第一个 property >= n 的块即"最小满足块"
 * 因此分配时"首次命中"就等价于"最优命中"，无需扫描整条链表比较大小。
 *
 * 代价是：既然链表按大小排序，块的首地址不再单调，
 *   - alloc 切分出来的剩余块、free 回来的块，都必须按新的大小重新定位插入；
 *   - free 时的左右物理邻块无法再由链表前后驱得到，必须按物理地址判断并查找。
 *
 * Page 状态约定（与 First-Fit 一致，测试函数依赖）：
 *   - 空闲块的首页(块头)：SetPageProperty(base) == 1，base->property == 块页数；
 *   - 块内非首页：PageProperty == 0，property 无意义；
 *   - 已被分配的块：PageProperty == 0（alloc 时 ClearPageProperty）。
 * */
static free_area_t best_fit_free_area;

#define free_list (best_fit_free_area.free_list)
#define nr_free  (best_fit_free_area.nr_free)

/* 按块大小(property)升序把一个空闲块插入 free_list。
 * 相同大小的块保持"先到者在前"（用严格大于号判断），不破坏任何断言，只是让行为可预测。 */
static void
best_fit_insert_by_size(struct Page *base) {
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property > base->property) {
            list_add_before(le, &(base->page_link));
            return;
        }
    }
    /* 没找到更大的块，说明它最大，追加到表尾 */
    list_add(list_prev(&free_list), &(base->page_link));
}

/* init - 只重置管理结构（空链表 + 计数清零），不涉及具体内存 */
static void
best_fit_init(void) {
    list_init(&free_list);
    nr_free = 0;
}

/* init_memmap - 把 [base, base + n) 这段可用物理内存纳入管理，建成一个空闲块 */
static void
best_fit_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        /* 传入的页应已被 page_init 标记为 reserved（尚未交给分配器） */
        assert(PageReserved(p));
        p->flags = 0;               /* 同时清掉 PG_reserved 与 PG_property */
        p->property = 0;
        set_page_ref(p, 0);
    }
    /* 只有块头记录整块页数 */
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    best_fit_insert_by_size(base);
}

/* alloc_pages - 分配连续的 n 个物理页，返回块头 Page；失败返回 NULL */
static struct Page *
best_fit_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    /* free_list 按 property 升序：第一个满足 property >= n 的块就是"最小满足块" */
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    if (page != NULL) {
        size_t block_size = page->property;
        list_del(&(page->page_link));
        if (block_size > n) {
            /* 从块头切出 n 页，剩余部分从 page + n 开始，仍是一个空闲块头 */
            struct Page *rest = page + n;
            rest->property = block_size - n;
            SetPageProperty(rest);
            /* 剩余块比原块小，位置不再确定，必须按大小重新定位插入 */
            best_fit_insert_by_size(rest);
        }
        nr_free -= n;
        /* 被分配的块不再是空闲块头 */
        ClearPageProperty(page);
    }
    return page;
}

/* free_pages - 回收 [base, base + n)，插回空闲链表并与物理左右邻块合并 */
static void
best_fit_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        /* 必须是已分配的普通页：不是保留页，也不是空闲块头 */
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;

    /* 1) 先在现有空闲链表中按物理地址找左右邻块。
     *    链表按大小排序，前后驱不再是物理邻居，只能逐个比较地址区间：
     *      q + q->property == base -> q 块尾正好紧挨 base（左邻）
     *      base + n == q            -> base 块尾正好紧挨 q（右邻）
     *    注意右邻必须用本次释放区间的末尾 base + n 来判断，不能套用
     *    base->property（后面合并会改写它）。空闲块互不重叠，故左右邻最多各一个。 */
    struct Page *left = NULL, *right = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *q = le2page(le, page_link);
        if (q + q->property == base) {
            left = q;
        }
        if (base + n == q) {
            right = q;
        }
    }

    /* 2) 先摘下邻块再合并，避免"已在链表中却改了 property"破坏有序性 */
    if (left != NULL) {
        list_del(&(left->page_link));
        ClearPageProperty(base);          /* base 不再是块头 */
        left->property += base->property; /* base->property 此刻仍是 n */
        base = left;                      /* 合并后的块头变成 left */
    }
    if (right != NULL) {
        list_del(&(right->page_link));
        base->property += right->property;
        ClearPageProperty(right);
    }

    /* 3) 合并完成后，按新的块大小插回 free_list */
    best_fit_insert_by_size(base);
}

/* nr_free_pages - 返回当前空闲页总数 */
static size_t
best_fit_nr_free_pages(void) {
    return nr_free;
}

// Framework tests: students must not modify basic_check or best_fit_check.
static void
basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_list));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    free_list = free_list_store;
    nr_free = nr_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

static void
best_fit_check(void) {
    int score = 0, sumscore = 6;
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count++, total += p->property;
    }
    assert(total == nr_free_pages());

    basic_check();

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    struct Page *p0 = alloc_pages(5), *p1, *p2;
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    // * - - * -
    free_pages(p0 + 1, 2);
    free_pages(p0 + 4, 1);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 1) && p0[1].property == 2);
    // * - - * *
    assert((p1 = alloc_pages(1)) != NULL);
    assert(alloc_pages(2) != NULL);
    assert(p0 + 4 == p1);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    p2 = p0 + 1;
    free_pages(p0, 5);
    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    assert(nr_free == 0);
    nr_free = nr_free_store;

    free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        count--, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
}

/* Best-Fit 管理器的注册结构：按 struct pmm_manager 的成员顺序/职责实现 */
const struct pmm_manager best_fit_pmm_manager = {
    .name          = "best_fit_pmm_manager",
    .init          = best_fit_init,
    .init_memmap   = best_fit_init_memmap,
    .alloc_pages   = best_fit_alloc_pages,
    .free_pages    = best_fit_free_pages,
    .nr_free_pages = best_fit_nr_free_pages,
    .check         = best_fit_check,
};
