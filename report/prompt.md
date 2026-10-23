## 2411858 王钟岳

### page_init()函数生成提示词

[PROMPT]

**任务**：修改文件 pmm.c（可以是多个文件），实现或修改 page_init 函数。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。完成函数的实现或修改，包括但不限于：函数主体、辅助函数、内部使用的静态变量等。

**输出要求**：直接进行文件操作。保留文件中的现有代码。实现或修改的代码应插入或更新到相应的位置。第一次实现时，修改到pmm.c的第65行

[RELY]

// 宏定义

框架提供的内存上界、地址转换、页面对齐和页面状态宏

// 数据结构

链接符号end、struct Page及相关全局变量

// 可用函数

get_memory_base()、get_memory_size()、panic()、cprintf()、init_memmap()、pa2page()

[GUARANTEE]

必须实现或修改的接口：

根据设备树提供的物理内存信息建立可管理物理页的描述符布局，并把排除 OpenSBI、内核镜像和描述符数组占用空间后的完整空闲页面交给当前pmm_manager管理，为后续页面分配提供正确的初始状态。

你可以根据需要添加：

- 辅助函数（建议设为static）
- 内部使用的宏定义
- 模块内部的静态变量

[SPECIFICATION]

**Pre-Condition**:

- npage 已有合法值、pages 指针已有值、pages[0..npage-1] 的内容 未初始化、free_list 空链表、nr_free为0

**Post-Condition**:

- pages[i].flags 先全部 reserved=1，再把可用区间清掉 reserved；pages[i].ref 全部清0；pages[i].property 空闲块首块=连续页数，非首块=0；空闲页的 page_link 串进 free_list；nr_free为正数。
  
  **Case 1**:
  - 区分虚拟地址、物理地址、物理页号和Page描述符地址，正确处理可管理上界和页面对齐，并确保内核及描述符数组不会进入空闲区

  **Case 2**:
  - 设备树信息无效时应终止初始化

  **Case 3**:
  - 没有可用完整页面时不得调用init_memmap()

**Requirements**:

- 中文注释干净简练，减少page_init函数无用的输出。



## 2412934 王海沛

```markdown
[PROMPT]
在 RISC-V 64 的 ucore 教学内核中实现 Best-Fit 连续物理内存管理器，使 make grade 的两项测试通过。

改哪里
1. kern/mm/best_fit_pmm.c —— 全部算法写在此文件，落点是 //lab2 function2-2 YOUR CODE。
2. kern/mm/pmm.c 的 init_pmm_manager() —— 改为 pmm_manager = &best_fit_pmm_manager，仅此一行。

怎么改
1. 空闲块按 property 升序挂 free_list，使"从表头第一个 property >= n 的块"即为最佳块，分配循环与 First-Fit 同形。
2. 切分后剩余块须按新大小重新定位插入，不能像 First-Fit 那样原地插回——排序键是大小不是地址。
3. 释放时按物理地址找左右邻合并，判据 q + q->property == base（左）与 base + n == q（右）；不能沿用 list_prev / list_next 当邻居，那只在按地址有序时成立。

[RELY]
以下均已存在，直接用，不要重复定义、不要新增 include。

一、数据结构
struct pmm_manager（pmm.h）：name / init / init_memmap / alloc_pages / free_pages / nr_free_pages / check，按此序初始化，成员名照抄。
struct Page（memlayout.h）：用到 flags、property、ref、page_link 四个字段。
free_area_t（pmm.h）：文件内已定义 best_fit_free_area 及宏 free_list、nr_free。

二、已有函数
链表（list.h）：list_init / list_add / list_add_before / list_del / list_next / list_prev / le2page。free_list 是哨兵，遍历终止于回到 &free_list。
页标志（memlayout.h）：SetPageReserved / ClearPageReserved / PageReserved、SetPageProperty / ClearPageProperty / PageProperty。
引用计数与计数（pmm.h）：set_page_ref、nr_free_pages。
测试：basic_check()、best_fit_check() 已实现，只可调用。

三、页状态约定（与 default_pmm.c 一致）
空闲块头：PageProperty == 1 且 property == 块页数；块内非头页：PageProperty == 0。
已分配页：PageProperty == 0 且 ref == 0；未交给分配器的页：PageReserved == 1。
不变式：所有块头 property 之和 == nr_free_pages()。

[GUARANTEE]
一、best_fit_pmm.c 中新增 6 个 static 函数
    best_fit_init
    best_fit_init_memmap
    best_fit_alloc_pages
    best_fit_free_pages
    best_fit_nr_free_pages
    best_fit_insert_by_size 

二、文件末尾新增注册结构（须置于 best_fit_check 定义之后）
    const struct pmm_manager best_fit_pmm_manager = {
        .name          = "best_fit_pmm_manager",
        .init          = best_fit_init,
        .init_memmap   = best_fit_init_memmap,
        .alloc_pages   = best_fit_alloc_pages,
        .free_pages    = best_fit_free_pages,
        .nr_free_pages = best_fit_nr_free_pages,
        .check         = best_fit_check,
    };

三、外部改动
    kern/mm/pmm.c 的 init_pmm_manager()：指向 best_fit_pmm_manager。

四、不得改动
    basic_check / best_fit_check 一字不改；不重复定义 free_list / nr_free。

[SPECIFICATION]
1. best_fit_init
   list_init(&free_list); nr_free = 0。只重置管理结构，不碰真实内存。

2. best_fit_insert_by_size(base)
   沿表找第一个 property > base->property 的块，list_add_before 插入；到表尾则追加。
   用严格大于，使同大小块保持先来者在前，分配顺序可复现。

3. best_fit_init_memmap(base, n)
   遍历 [base, base+n)：assert(PageReserved(p))，再 p->flags = 0（一并清掉 Reserved 与 Property）、
   property = 0、ref = 0。
   仅块头：base->property = n，SetPageProperty(base)。
   nr_free += n。 
   

4. best_fit_alloc_pages(n)
    n > nr_free 直接返回 NULL。 
    顺序扫描，首个 property >= n 即最优块。
    先记 block_size 再 list_del 摘链。  
    block_size > n 时：rest = page + n，rest->property = block_size - n，SetPageProperty(rest)，按大小插回。
    nr_free -= n；ClearPageProperty(page)。未命中返回 NULL。

5. best_fit_free_pages(base, n)
    逐页 assert(!PageReserved(p) && !PageProperty(p))，然后 flags = 0、ref = 0。
    base->property = n；SetPageProperty(base)；nr_free += n。
    全表扫描找邻居：q + q->property == base 为左邻，base + n == q 为右邻。
      右邻必须用 base + n，不能用 base->property（左合并会改写它）。
    先左后右合并，每次先 list_del 摘链再改 property；左合并后 base = left。
    按最终大小插回链表。

6. best_fit_nr_free_pages
   return nr_free，O(1)。

7. 交付前自查
    遍历各块头 PageProperty 为真，Σproperty == nr_free_pages()。
    释放 p0+1 起 2 页、p0+4 起 1 页后，alloc_pages(1) 必须返回 p0+4 而非 p0+1。
     这是 Best-Fit 的语义核心：按地址排序的实现在此必败。
    free_pages(p0, 5) 后 alloc_pages(5) 须成功，验证三方合并。
```

## 2412143 林家熠

**第一份 Prompt（用于生成 Buddy System 模块并接入内核）：**

```markdown
[PROMPT]
修改文件 kern/mm/buddy_pmm.c 和 kern/mm/pmm.c，从零实现完整的 Buddy System 物理内存分配模块并完成接入。

[RELY]
// 宏定义与数据结构
struct Page 的 property 字段、libs/list.h 中的双向链表宏、PPN 宏定义。
// 已有框架
kern/mm/pmm.h 中的 struct pmm_manager 框架。

[GUARANTEE]
必须实现或修改的接口：
- buddy_init
- buddy_init_memmap
- buddy_alloc_pages
- buddy_free_pages
- buddy_nr_free_pages
- 定义 struct pmm_manager buddy_pmm_manager
修改 kern/mm/pmm.c 的 init_pmm_manager 函数，将 pmm_manager 指向 buddy_pmm_manager。

[SPECIFICATION]
Pre-Condition: 系统已经通过 page_init 探测到物理内存范围。
Post-Condition:
- 定义 #define MAX_ORDER 11（最大支持 2^11 个连续页）。
- 使用 struct Page 的 property 字段记录块的阶数（order），头页的 property 有效。
- 实现 free_area[MAX_ORDER + 1] 双向链表数组，每个节点挂在对应阶数的空闲块。
- buddy_init：初始化 free_area 链表。
- buddy_init_memmap：将 [base, base + n) 的空闲页按 2 的幂次拆分，挂入 free_area。
- buddy_alloc_pages：申请 n 页时向上取整到 2 的 order 次幂，从 free_area[order] 查找，找不到则分裂更大块。
- buddy_free_pages：释放块并检查伙伴块（通过 PPN 异或计算伙伴页号），如果伙伴空闲则合并。
- buddy_nr_free_pages：返回所有阶数空闲页总数。
```

**第二份 Prompt（用于生成测试函数）：**

```markdown
[PROMPT]
修改文件 kern/mm/buddy_pmm.c，编写测试函数 buddy_check()。

[RELY]
刚才生成的 Buddy System 分配器、assert 宏、cprintf 函数。

[GUARANTEE]
必须实现或修改的接口：
- buddy_check() 测试函数。

[SPECIFICATION]
测试行为要求覆盖以下核心行为与边界情况：
1. 基本分配与释放：申请 1 页、2 页、3 页（验证向上取整到 4 页）、8 页，检查返回的页是否合法且不重叠。
2. 伙伴合并：依次释放相邻的伙伴块（如先释放 A，再释放其伙伴 B），验证它们是否被正确合并为更大的块。
3. 边界情况：尝试申请超过 MAX_ORDER 大小的页（如 1 << (MAX_ORDER + 1)），应返回 NULL；释放时传入非对齐的页数应触发 assert 失败。
4. 完整性检查：在测试前后检查 buddy_nr_free_pages() 的总数是否一致。
请输出完整的测试代码，使用 assert 和 cprintf 进行验证和输出。
```

**SLUB模块（challenge）：**

**第一份 Prompt（用于生成 SLUB 模块并接入内核）：**

```markdown
[PROMPT]
新建文件 kern/mm/slub.c 和 kern/mm/slub.h，从零实现一个简化的 SLUB 内存分配器，并在 kern/mm/pmm.c 的 pmm_init 末尾接入。

[RELY]
// 宏定义与数据结构
struct Page、page2pa、va_pa_offset。
// 已有函数
现有的页分配器（如 default_pmm_manager 的 alloc_pages）。

[GUARANTEE]
必须实现或修改的接口：
- slub_init
- kmalloc(size_t size)
- kfree(void *ptr)
- 内部结构体：kmem_cache 数组。
修改 kern/mm/pmm.c 的 pmm_init 末尾调用 slub_init()。

[SPECIFICATION]
Pre-Condition: 物理内存页分配器已初始化完毕。
Post-Condition:
- 支持分配指定大小的对象（32, 64, 128, 256, 512 字节），向上对齐到 2 的幂次。
- 内部维护一个 kmem_cache 数组，每种大小对应一个 cache。
- 当 cache 的空闲链表为空时，调用页分配器申请一页，并将其切分为对应大小的对象挂入空闲链表。
- 为了在 kfree 时能知道对象大小，请在每个内存块前面添加一个 Header 元数据，记录该块所属的 cache。
```

**第二份 Prompt（用于生成测试函数）：**

```markdown
[PROMPT]
修改文件 kern/mm/slub.c，编写测试函数 slub_check()，并在 kern/mm/pmm.c 的 pmm_init 中调用。

[RELY]
刚才生成的 SLUB 分配器、assert 宏、cprintf 函数。

[GUARANTEE]
必须实现或修改的接口：
- slub_check() 测试函数。

[SPECIFICATION]
测试行为要求覆盖以下核心行为与边界情况：
1. 基础分配测试：测试分配不同大小（如 32B, 50B, 200B）的对象，并验证返回的指针不为空，且能通过 Header 验证其进入了正确的 Cache 大小。
2. 批量分配测试：连续分配 100 个 128B 的对象，验证是否触发了底层向页分配器申请新页（Refill）。
3. 释放与复用测试：释放刚才分配的所有对象，检查对应 Cache 的空闲对象数是否正确增加。
4. 边界测试：分配大于最大支持范围（如 1024B）和大小为 0 的对象，应返回 NULL。
请使用 assert 和 cprintf 输出测试结果。
```
