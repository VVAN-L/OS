#include <default_pmm.h>
#include <defs.h>
#include <error.h>
#include <kmalloc.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <vmm.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// The kernel image is mapped at VA=KERNBASE and PA=info.base
uint_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
pde_t *boot_pgdir_va = NULL;
// physical address of boot-time page directory
uintptr_t boot_pgdir_pa;

// physical memory management
const struct pmm_manager *pmm_manager;

static void check_alloc_page(void);
static void check_pgdir(void);
static void check_boot_pgdir(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void)
{
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n)
{
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n)
{
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void)
{
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

//lab2 请补充你在lab2中的代码
static void page_init(void) {
    uint64_t memory_base = get_memory_base();
    uint64_t memory_size = get_memory_size();

    // 内存区间必须有效，并覆盖内核的物理起点。
    if (memory_size == 0 || memory_base > (uint64_t)-1 - memory_size ||
        memory_base > KERNEL_BEGIN_PADDR) {
        panic("Invalid physical memory information");
    }
    uint64_t memory_end = memory_base + memory_size;
    if (memory_end > PHYSICAL_MEMORY_END) {
        memory_end = PHYSICAL_MEMORY_END;
    }
    uintptr_t maxpa = ROUNDDOWN(memory_end, PGSIZE);
    if (maxpa <= KERNEL_BEGIN_PADDR) {
        panic("No manageable physical memory");
    }

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    extern char end[];
    uintptr_t kernel_end = PADDR(end);
    if (kernel_end < KERNEL_BEGIN_PADDR || kernel_end >= maxpa) {
        panic("Kernel exceeds physical memory");
    }

    // 描述符紧随内核；npage 是页号上界，而非描述符数量。
    npage = maxpa / PGSIZE;
    size_t page_count = npage - nbase;
    uintptr_t pages_pa = ROUNDUP(kernel_end, PGSIZE);
    if (page_count > (maxpa - pages_pa) / sizeof(struct Page)) {
        panic("Insufficient memory for page descriptors");
    }
    uintptr_t free_begin = ROUNDUP(
        pages_pa + page_count * sizeof(struct Page), PGSIZE);
    if (free_begin >= maxpa) {
        panic("No complete free physical page");
    }

    pages = (struct Page *)(pages_pa + va_pa_offset);
    for (size_t i = 0; i < page_count; i++) {
        pages[i].flags = 0;
        SetPageReserved(&pages[i]);
        set_page_ref(&pages[i], 0);
        pages[i].property = 0;
        list_init(&pages[i].page_link);
    }

    // 仅交出内核和描述符之后的完整页面，由管理器建立空闲块。
    init_memmap(pa2page(free_begin), (maxpa - free_begin) / PGSIZE);
}

static void enable_paging(void)
{
    write_csr(satp, 0x8000000000000000 | (boot_pgdir_pa >> RISCV_PGSHIFT));
}

// boot_map_segment - setup&enable the paging mechanism
// parameters
//  la:   linear address of this memory need to map (after x86 segment map)
//  size: memory size
//  pa:   physical address of this memory
//  perm: permission of this memory
static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size,
                             uintptr_t pa, uint32_t perm)
{
    assert(PGOFF(la) == PGOFF(pa));
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    for (; n > 0; n--, la += PGSIZE, pa += PGSIZE)
    {
        pte_t *ptep = get_pte(pgdir, la, 1);
        assert(ptep != NULL);
        *ptep = pte_create(pa >> PGSHIFT, PTE_V | perm);
    }
}

// boot_alloc_page - allocate one page using pmm->alloc_pages(1)
// return value: the kernel virtual address of this allocated page
// note: this function is used to get the memory for PDT(Page Directory
// Table)&PT(Page Table)
static void *boot_alloc_page(void)
{
    struct Page *p = alloc_page();
    if (p == NULL)
    {
        panic("boot_alloc_page failed.\n");
    }
    return page2kva(p);
}

// pmm_init - setup a pmm to manage physical memory, build PDT&PT to setup
// paging mechanism
//         - check the correctness of pmm & paging mechanism, print PDT&PT
void pmm_init(void)
{
    // We need to alloc/free the physical memory (granularity is 4KB or other
    // size).
    // So a framework of physical memory manager (struct pmm_manager)is defined
    // in pmm.h
    // First we should init a physical memory manager(pmm) based on the
    // framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a
    // pmm
    check_alloc_page();
    // create boot_pgdir, an initial page directory(Page Directory Table, PDT)
    extern char boot_page_table_sv39[];
    boot_pgdir_va = (pte_t *)boot_page_table_sv39;
    boot_pgdir_pa = PADDR(boot_pgdir_va);

    check_pgdir();

    static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);

    // now the basic virtual memory map(see memalyout.h) is established.
    // check the correctness of the basic virtual memory map.
    check_boot_pgdir();

    kmalloc_init();
}

// get_pte - get pte and return the kernel virtual address of this pte for la
//        - if the PT contians this pte didn't exist, alloc a page for PT
// parameter:
//  pgdir:  the kernel virtual base address of PDT
//  la:     the linear address need to map
//  create: a logical value to decide if alloc a page for PT
// return vaule: the kernel virtual address of this pte
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    pde_t *pdep1 = &pgdir[PDX1(la)];
    if (!(*pdep1 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    pde_t *pdep0 = &((pte_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
    if (!(*pdep0 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}

// get_page - get related Page struct for linear address la using PDT pgdir
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep_store != NULL)
    {
        *ptep_store = ptep;
    }
    if (ptep != NULL && *ptep & PTE_V)
    {
        return pte2page(*ptep);
    }
    return NULL;
}

//LAB4 function1 2411858
static void mapping_tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    // 保证页表写入不会被编译器移到 TLB 刷新之后。
    asm volatile("" ::: "memory");
    tlb_invalidate(pgdir, la);
    asm volatile("" ::: "memory");
}

void page_remove(pde_t *pgdir, uintptr_t la)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep == NULL || !(*ptep & PTE_V))
    {
        return;
    }
    struct Page *page = pte2page(*ptep);
    *ptep = 0;
    mapping_tlb_invalidate(pgdir, la);
    if (page_ref_dec(page) == 0)
    {
        free_page(page);
    }
}

int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    pde_t *pdep = &pgdir[PDX1(la)];
    pde_t old_pde = *pdep;
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL)
    {
        // get_pte 可能只建好一级页表，失败时回收本次新增部分。
        if (!(old_pde & PTE_V) && (*pdep & PTE_V))
        {
            struct Page *table = pde2page(*pdep);
            *pdep = old_pde;
            mapping_tlb_invalidate(pgdir, la);
            set_page_ref(table, 0);
            free_page(table);
        }
        return -E_NO_MEM;
    }

    // 先增加引用，同页重映射时抵消，避免误释放目标页。
    page_ref_inc(page);
    if (*ptep & PTE_V)
    {
        if (pte2page(*ptep) == page)
        {
            page_ref_dec(page);
        }
        else
        {
            page_remove(pgdir, la);
        }
    }
    *ptep = pte_create(page2ppn(page), perm);
    mapping_tlb_invalidate(pgdir, la);
    return 0;
}







// invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
void tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    // flush_tlb();
    // The flush_tlb flush the entire TLB, is there any better way?
    asm volatile("sfence.vma %0" : : "r"(la));
}

static void check_alloc_page(void)
{
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

static void check_pgdir(void)
{
    // assert(npage <= KMEMSIZE / PGSIZE);
    // The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE
    size_t nr_free_store;

    nr_free_store = nr_free_pages();

    assert(npage <= KERNTOP / PGSIZE);
    assert(boot_pgdir_va != NULL && (uint32_t)PGOFF(boot_pgdir_va) == 0);
    assert(get_page(boot_pgdir_va, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();
    assert(page_insert(boot_pgdir_va, p1, 0x0, 0) == 0);

    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir_va, 0x0, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert(page_ref(p1) == 1);

    ptep = (pte_t *)KADDR(PDE_ADDR(boot_pgdir_va[0]));
    ptep = (pte_t *)KADDR(PDE_ADDR(ptep[0])) + 1;
    assert(get_pte(boot_pgdir_va, PGSIZE, 0) == ptep);

    p2 = alloc_page();
    assert(page_insert(boot_pgdir_va, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);
    assert(*ptep & PTE_W);
    assert(boot_pgdir_va[0] & PTE_U);
    assert(page_ref(p2) == 1);

    assert(page_insert(boot_pgdir_va, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);
    assert(page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);

    page_remove(boot_pgdir_va, 0x0);
    assert(page_ref(p1) == 1);
    assert(page_ref(p2) == 0);

    page_remove(boot_pgdir_va, PGSIZE);
    assert(page_ref(p1) == 0);
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir_va[0])) == 1);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_pgdir() succeeded!\n");
}

static void check_boot_pgdir(void)
{
    size_t nr_free_store;
    pte_t *ptep;
    int i;

    nr_free_store = nr_free_pages();

    for (i = ROUNDDOWN(KERNBASE, PGSIZE); i < npage * PGSIZE; i += PGSIZE)
    {
        assert((ptep = get_pte(boot_pgdir_va, (uintptr_t)KADDR(i), 0)) != NULL);
        assert(PTE_ADDR(*ptep) == i);
    }

    assert(boot_pgdir_va[0] == 0);

    struct Page *p;
    p = alloc_page();
    assert(page_insert(boot_pgdir_va, p, 0x100, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 1);
    assert(page_insert(boot_pgdir_va, p, 0x100 + PGSIZE, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 2);

    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    *(char *)(page2kva(p) + 0x100) = '\0';
    assert(strlen((const char *)0x100) == 0);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(p);
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_boot_pgdir() succeeded!\n");
}

// perm2str - use string 'u,r,w,-' to present the permission
static const char *perm2str(int perm)
{
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = 'r';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

// get_pgtable_items - In [left, right] range of PDT or PT, find a continuous
// linear addr space
//                  - (left_store*X_SIZE~right_store*X_SIZE) for PDT or PT
//                  - X_SIZE=PTSIZE=4M, if PDT; X_SIZE=PGSIZE=4K, if PT
// paramemters:
//  left:        no use ???
//  right:       the high side of table's range
//  start:       the low side of table's range
//  table:       the beginning addr of table
//  left_store:  the pointer of the high side of table's next range
//  right_store: the pointer of the low side of table's next range
//  return value: 0 - not a invalid item range, perm - a valid item range with
//  perm permission
static int get_pgtable_items(size_t left, size_t right, size_t start,
                             uintptr_t *table, size_t *left_store,
                             size_t *right_store)
{
    if (start >= right)
    {
        return 0;
    }
    while (start < right && !(table[start] & PTE_V))
    {
        start++;
    }
    if (start < right)
    {
        if (left_store != NULL)
        {
            *left_store = start;
        }
        int perm = (table[start++] & PTE_USER);
        while (start < right && (table[start] & PTE_USER) == perm)
        {
            start++;
        }
        if (right_store != NULL)
        {
            *right_store = start;
        }
        return perm;
    }
    return 0;
}
