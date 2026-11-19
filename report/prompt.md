## 2411858 王钟岳

[PROMPT]

**任务**：修改文件 mm/pmm.c，实现 page_insert、page_remove 函数。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。完成函数的实现或修改，包括但不限于：函数主体、辅助函数、内部使用的静态变量等。

**输出要求**：直接进行文件操作。不要修改文件中的现有代码。实现或修改的代码应插入或更新到相应的位置（224行）。

[RELY]

这个模块主要依赖 get_pte()、pte_create()、pte2page()、page_ref_inc()、page_ref_dec()、set_page_ref()、alloc_page()、free_page()、tlb_invalidate()、flush_tlb() 等现成接口，也依赖 pde_t、pte_t、struct Page、pages、boot_pgdir_va 以及页表权限位宏 PTE_V、PTE_U、PTE_W、PTE_R。其中 get_pte() 负责定位目标页表项，后续两个函数则在此基础上完成映射建立和映射删除。

[GUARANTEE]

必须实现或修改的接口：
对外只需要提供“建立映射”和“撤销映射”两个动作。

你可以根据需要添加：

- 辅助函数（建议设为static）
- 内部使用的宏定义
- 模块内部的静态变量

[SPECIFICATION]

整个功能模块的具体功能为：这个模块负责在现有多级页表框架上完成最基本的映射维护，也就是把物理页挂到指定线性地址上，以及把已有映射安全地撤销掉。page_insert 负责建立或更新映射，page_remove 负责删除映射，它们共同决定一个线性地址最终对应哪一页物理内存，同时也决定物理页引用计数是否正确、页表状态是否一致、TLB 是否需要失效。这个模块完成后，页表测试、静态内核映射检查以及后续线程地址空间相关逻辑才有可靠基础。

**Case 1**:
page_insert 成功时应让指定线性地址稳定映射到目标物理页，并把权限位设置为调用者要求的状态

**Case 2**:
page_insert 失败时应返回 -E_NO_MEM，且不能留下半完成映射。

**Case 3**:
page_remove 则应作为一个幂等删除接口存在，目标地址有映射时删除映射、无映射时安静返回，不应把“未映射地址”当作异常情况。

**Requirements**:

这里最关键的是保持“页表项内容、物理页引用计数、TLB 状态”三者同步。page_insert 不能简单覆盖旧项，而要正确处理原地址已经映射到同一物理页、或者原地址已经映射到其他物理页这两种情况；page_remove 也不能假定目标地址一定存在有效映射。由于 get_pte(pgdir, la, 1) 可能需要分配中间级页表页，所以映射建立过程本身可能失败，调用路径必须能正确返回错误并保持系统状态干净。所有页表更新完成后都要考虑对应 TLB 失效，否则测试中会出现看似已更新、实际上仍在使用旧翻译结果的情况。

## 2412143 林家熠

```markdown
[PROMPT]
修改文件 kern/process/proc.c，实现进程创建及初始化功能。补全 alloc_proc, proc_init, kernel_thread, do_fork, copy_thread, copy_mm 六个函数，成功创建 idleproc 和 initproc 两个内核线程。

[RELY]
1. 数据结构：struct proc_struct, struct context, struct trapframe, list_entry_t。
2. 全局变量：proc_list, hash_list, idleproc, initproc, current, nr_process, boot_pgdir_pa。
3. 已有接口：kmalloc, memset, list_init, read_csr(sstatus), local_intr_save/restore, setup_kstack, get_pid, hash_proc。
4. 汇编符号：bootstack, kernel_thread_entry, forkret。

[GUARANTEE]
必须实现或修改的接口：
- static struct proc_struct *alloc_proc(void);
- void proc_init(void);
- int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
- int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
- static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
- static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
严禁修改函数签名，严禁破坏原有的全局变量定义。

[SPECIFICATION]
**Pre-Condition**: 物理内存管理和中断系统已初始化完毕，可以调用 kmalloc 和分配物理页。
**Post-Condition**:
1. alloc_proc: 分配 PCB，初始化状态为 PROC_UNINIT，pid=-1，pgdir=boot_pgdir_pa，初始化链表节点，将 context 和 name 清零。
2. proc_init: 初始化 proc_list 和 hash_list；创建 idleproc 并设为 PROC_RUNNABLE，pid=0，kstack=bootstack，need_resched=1，current=idleproc；通过 kernel_thread 创建 initproc，加入链表并设为 RUNNABLE。
3. kernel_thread: 构造临时 trapframe，设置 s0=fn, s1=arg；读取 sstatus，清除 SIE 位，设置 SPP 和 SPIE 位；epc 指向 kernel_thread_entry；调用 do_fork。
4. do_fork: 依次调用 alloc_proc, setup_kstack, copy_mm, copy_thread；使用 local_intr_save 保护临界区，分配 pid，加入全局链表和哈希表，设为 PROC_RUNNABLE。注意处理分配失败时的回滚清理。
5. copy_thread: 在内核栈顶分配 tf 空间并复制 tf，设置 a0=0 (子进程返回值)，若 esp 为 0 则 sp 指向 tf；设置 context.ra=forkret，context.sp=tf。
6. copy_mm: 当前为空实现，直接返回 0。
**Requirements**: 注意 PCB 生命周期状态与资源挂接的顺序，防止内存泄漏。
```

## 2412934 王海沛

```markdown
[PROMPT]

**任务**：修改文件 mm/pmm.c，实现 page_insert、page_remove 函数。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。完成函数的实现或修改，包括但不限于：函数主体、辅助函数、内部使用的静态变量等。

**输出要求**：直接进行文件操作。不要修改文件中的现有代码。实现或修改的代码应插入或更新到相应的位置（224行）。

[RELY]

这个模块主要依赖 get_pte()、pte_create()、pte2page()、page_ref_inc()、page_ref_dec()、set_page_ref()、alloc_page()、free_page()、tlb_invalidate()、flush_tlb() 等现成接口，也依赖 pde_t、pte_t、struct Page、pages、boot_pgdir_va 以及页表权限位宏 PTE_V、PTE_U、PTE_W、PTE_R。其中 get_pte() 负责定位目标页表项，后续两个函数则在此基础上完成映射建立和映射删除。

[GUARANTEE]

必须实现或修改的接口：
对外只需要提供“建立映射”和“撤销映射”两个动作。

你可以根据需要添加：

- 辅助函数（建议设为static）
- 内部使用的宏定义
- 模块内部的静态变量

[SPECIFICATION]

整个功能模块的具体功能为：这个模块负责在现有多级页表框架上完成最基本的映射维护，也就是把物理页挂到指定线性地址上，以及把已有映射安全地撤销掉。page_insert 负责建立或更新映射，page_remove 负责删除映射，它们共同决定一个线性地址最终对应哪一页物理内存，同时也决定物理页引用计数是否正确、页表状态是否一致、TLB 是否需要失效。这个模块完成后，页表测试、静态内核映射检查以及后续线程地址空间相关逻辑才有可靠基础。

**Case 1**:
page_insert 成功时应让指定线性地址稳定映射到目标物理页，并把权限位设置为调用者要求的状态

**Case 2**:
page_insert 失败时应返回 -E_NO_MEM，且不能留下半完成映射。

**Case 3**:
page_remove 则应作为一个幂等删除接口存在，目标地址有映射时删除映射、无映射时安静返回，不应把“未映射地址”当作异常情况。

**Requirements**:

这里最关键的是保持“页表项内容、物理页引用计数、TLB 状态”三者同步。page_insert 不能简单覆盖旧项，而要正确处理原地址已经映射到同一物理页、或者原地址已经映射到其他物理页这两种情况；page_remove 也不能假定目标地址一定存在有效映射。由于 get_pte(pgdir, la, 1) 可能需要分配中间级页表页，所以映射建立过程本身可能失败，调用路径必须能正确返回错误并保持系统状态干净。所有页表更新完成后都要考虑对应 TLB 失效，否则测试中会出现看似已更新、实际上仍在使用旧翻译结果的情况。
```
