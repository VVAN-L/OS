#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    //注释掉原来的pmm_manager = &default_pmm_manager，并且换成pmm_manager = &buddy_pmm_manager;
    //源代码 pmm_manager = &default_pmm_manager;
    //任务三 pmm_manager = &buddy_pmm_manager;
    pmm_manager = &best_fit_pmm_manager; //任务二
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 YOUR CODE

/* page_init - 探测物理内存，建立页描述符数组，并把剩余可用内存交给分配器
 *
 * 步骤：
 *   1. 从设备树(dtb)取物理内存的起始地址和大小；
 *   2. 计算总页数 npage、页数组自身需要的页数，把 pages 放在内核 end 之后；
 *   3. 先全部标记为 reserved，再逐段把空闲区交给 pmm->init_memmap；
 *   4. 被内核镜像和页数组占用的页保持 reserved，不交给分配器。
 * */
void page_init(void) {
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    uint64_t mem_end  = mem_base + mem_size;

    // 内核被映射到 KERNBASE，虚拟地址与物理地址相差固定的偏移量
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // npage 是"最大物理页号 + 1"，页号 = 物理地址 >> PGSHIFT
    npage = PPN(mem_end);

    // 页描述符数组本身也要占物理内存，大小 = npage * sizeof(struct Page)，按页向上取整
    uintptr_t pages_size = ROUNDUP(npage * sizeof(struct Page), PGSIZE);

    // 把页数组紧挨着内核镜像的末尾摆放（end 是内核 BSS 之后的虚拟地址）
    extern char end[];
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    memset(pages, 0, pages_size);

    // 逐页初始化：先把每一页都标成 reserved（默认谁都不能用）
    for (struct Page *p = pages; p < pages + npage - nbase; p ++) {
        p->flags = 0;
        p->property = 0;
        p->ref = 0;
        SetPageReserved(p);
    }

    // 物理内存中可交给分配器的部分：[物理内存起点, 内核起点的前一页)
    // 以及 [页数组末尾, 物理内存末尾)，两段都必须是连续的整块
    struct Page *kern_begin = pa2page(KERNEL_BEGIN_PADDR);   // 内核镜像起始页（虚拟地址）
    struct Page *pages_end  = (struct Page *)ROUNDUP((uintptr_t)pages + pages_size, PGSIZE);

    // 低地址段：从 DRAM 起点到内核起点
    struct Page *mem_begin = pa2page(mem_base);
    if (kern_begin > mem_begin) {
        init_memmap(mem_begin, kern_begin - mem_begin);
    }

    // 高地址段：从页数组末尾到物理内存末尾
    // 注意 mem_end 是区间的"右端点"（不含），要减 1 才落在最后一个合法页内，
    // 否则 PPN(mem_end) == npage，pa2page 会判定越界。
    struct Page *mem_last = pa2page(mem_end - 1) + 1;
    if (mem_last > pages_end) {
        init_memmap(pages_end, mem_last - pages_end);
    }

    // 注意：内核镜像本身 [KERNEL_BEGIN_PADDR, pages_end) 以及页数组所占的页
    // 始终保持 reserved，不会被交给分配器，符合 test_page_init 的检查要求。
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
