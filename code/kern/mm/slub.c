#include <slub.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define NUM_CACHES 5
#define ALIGN_SIZE 8 // 8字节对齐

// 供内部空闲链表使用的节点
struct slub_free_node {
    struct slub_free_node *next;
};

// 每一个内存块前面的元数据头部（Header）
// 这样释放时，只需向前偏移 sizeof(header) 就能知道它属于哪个 cache
struct slub_header {
    size_t size;               // 对象实际大小
    struct kmem_cache *cache;  // 所属的 cache
};

// Cache 结构体
struct kmem_cache {
    size_t obj_size;                  // 对象大小
    struct slub_free_node *free_list; // 空闲对象链表
    size_t total_objs;                // 该 cache 已向系统申请的总对象数
    size_t free_objs;                 // 该 cache 当前空闲对象数
};

static struct kmem_cache caches[NUM_CACHES];
static size_t cache_sizes[NUM_CACHES] = {32, 64, 128, 256, 512};

// 对大小进行向上对齐
static size_t align_up(size_t size) {
    return (size + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1);
}

void slub_init(void) {
    for (int i = 0; i < NUM_CACHES; i++) {
        caches[i].obj_size = cache_sizes[i];
        caches[i].free_list = NULL;
        caches[i].total_objs = 0;
        caches[i].free_objs = 0;
    }
    cprintf("SLUB: Initialized %d caches (32B to 512B).\n", NUM_CACHES);
}

// 当 cache 空间不足时，向底层伙伴系统（或 First-Fit）申请一页并切分
static void slub_refill(struct kmem_cache *cache) {
    // 1. 申请一页 (4KB)
    struct Page *page = alloc_pages(1);
    if (page == NULL) {
        cprintf("SLUB: FATAL! Out of physical pages.\n");
        return;
    }
    
    // 2. 获取这页对应的内核虚拟地址
    void *page_addr = (void *)(page2pa(page) + va_pa_offset);
    
    // 3. 计算每个实际块的大小（包括 Header 的大小）
    size_t total_size = align_up(cache->obj_size + sizeof(struct slub_header));
    
    // 4. 计算一页能切分出多少个对象
    int num_objs = PGSIZE / total_size;
    
    // 5. 切分并串入空闲链表
    for (int i = 0; i < num_objs; i++) {
        // 预留出 Header 的空间，实际的指针往后退
        void *obj_ptr = (char *)page_addr + i * total_size;
        struct slub_header *header = (struct slub_header *)obj_ptr;
        header->size = cache->obj_size;
        header->cache = cache;
        
        // 将 Header 之后的空间作为空闲节点
        struct slub_free_node *node = (struct slub_free_node *)((char *)obj_ptr + sizeof(struct slub_header));
        node->next = cache->free_list;
        cache->free_list = node;
    }
    
    cache->total_objs += num_objs;
    cache->free_objs += num_objs;
    cprintf("SLUB: Refilled cache %luB, added %d objs.\n", cache->obj_size, num_objs);
}

void *kmalloc(size_t size) {
    if (size == 0 || size > 512) return NULL;
    
    // 找到最合适的 cache
    int idx = 0;
    while (idx < NUM_CACHES && cache_sizes[idx] < size) {
        idx++;
    }
    if (idx >= NUM_CACHES) return NULL;
    
    struct kmem_cache *cache = &caches[idx];
    
    // 如果没空闲对象了，触发扩容
    if (cache->free_list == NULL) {
        slub_refill(cache);
    }
    
    // 如果扩容后还是没空间（比如内存耗尽），返回 NULL
    if (cache->free_list == NULL) return NULL;
    
    // 从空闲链表摘取一个节点
    struct slub_free_node *node = cache->free_list;
    cache->free_list = node->next;
    cache->free_objs--;
    
    // 因为 header 刚好在 node 前面，通过 node 推导出 header
    struct slub_header *header = (struct slub_header *)((char *)node - sizeof(struct slub_header));
    
    // 将 header 之后的内存返回给用户
    return (void *)node;
}

void kfree(void *ptr) {
    if (ptr == NULL) return;
    
    // 1. 通过指针回退，找到对应的 Header
    struct slub_header *header = (struct slub_header *)((char *)ptr - sizeof(struct slub_header));
    struct kmem_cache *cache = header->cache;
    
    // 安全检查（防止非法指针或重复释放导致崩溃）
    if (cache == NULL || header->size == 0) {
        cprintf("SLUB: Warning! Invalid pointer free.\n");
        return;
    }
    
    // 2. 将内存块作为空闲节点，插回链表头部
    struct slub_free_node *node = (struct slub_free_node *)ptr;
    node->next = cache->free_list;
    cache->free_list = node;
    
    cache->free_objs++;
}

void slub_check(void) {
    cprintf("\n--- SLUB Test Start ---\n");
    
    // 1. 基础分配测试
    void *p1 = kmalloc(32);
    void *p2 = kmalloc(50);  // 应该对齐到 64B 的 cache
    void *p3 = kmalloc(200); // 应该对齐到 256B 的 cache
    assert(p1 != NULL && p2 != NULL && p3 != NULL);
    
    // 验证不同大小的对象是否进入了不同的 Cache
    struct slub_header *h1 = (struct slub_header *)((char *)p1 - sizeof(struct slub_header));
    struct slub_header *h2 = (struct slub_header *)((char *)p2 - sizeof(struct slub_header));
    assert(h1->cache->obj_size == 32);
    assert(h2->cache->obj_size == 64);
    cprintf("SLUB: Basic allocation passed.\n");
    
    // 2. 连续大量分配测试（触发 Refill）
    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = kmalloc(128);
        assert(ptrs[i] != NULL);
    }
    cprintf("SLUB: Bulk allocation (100 objs) passed.\n");
    
    // 3. 释放与复用测试
    size_t free_before = caches[2].free_objs; // 128B对应的cache索引
    kfree(p1);
    kfree(p2);
    kfree(p3);
    for (int i = 0; i < 100; i++) {
        kfree(ptrs[i]);
    }
    assert(caches[2].free_objs == free_before + 100);
    cprintf("SLUB: Free and reclaim passed.\n");
    
    // 4. 边界测试
    void *big = kmalloc(1024);
    assert(big == NULL);
    void *zero = kmalloc(0);
    assert(zero == NULL);
    cprintf("SLUB: Boundary checks passed.\n");
    
    cprintf("--- SLUB Test All Success! ---\n");
}