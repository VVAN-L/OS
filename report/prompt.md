# 第三次实验提示词

## 2411858 王钟岳

[PROMPT]

**任务**：
修改文件 kern\trap\trapentry.S，实现或修改 RESTORE_ALL、\_\_alltraps 模块。

**操作要求**：
你必须在项目中修改实际文件，而不是仅展示代码。完成功能模块的实现或修改，包括但不限于：函数主体、辅助函数、内部使用的静态变量等。

**输出要求**：
直接进行文件操作。保留文件中的现有代码。实现或修改的代码应插入或更新到60行 70行

[RELY]

这个模块主要依赖 struct trapframe、trap()、已经提供的 SAVE_ALL、**alltraps、**trapret、RESTORE_ALL 以及 RISC-V 函数调用约定和特权级 Trap 返回机制。其中，struct trapframe 决定保存后的上下文数据组织形式，SAVE_ALL 是已经提供的上下文保存参考，trap(struct trapframe \*tf) 是 C 语言层面的 Trap 处理接口，而 sret 是最终完成 Trap 返回的处理器特权指令。

[GUARANTEE]

必须实现的功能：

完成 Trap 从“进入内核”到“返回原执行流”之间的底层上下文管理；整个功能模块的注意事项为：这里最关键的是保持“Trap frame 布局、上下文保存与恢复、函数调用约定、Trap 返回状态”之间的一致性。整个功能模块应该满足的对外的接口为：Trap 发生以后，系统应能够通过 \_\_alltraps 建立完整的 Trap frame，并将其作为 struct trapframe \* 传递给：

```c
void trap(struct trapframe *tf);
```

trap() 返回以后，应能够进入 \_\_trapret，完成上下文恢复并通过 sret 返回。

你可以根据需要添加：

- 辅助函数（建议设为static）
- 内部使用的宏定义
- 模块内部的静态变量

[SPECIFICATION]

RESTORE_ALL

使被打断程序的执行状态能够从 Trap frame 中恢复出来。

必须与已经提供的 SAVE_ALL 保持对应关系，不能自行假设一个与实际 Trap frame 不一致的数据布局。特别需要注意，SAVE_ALL 中对 x2(sp) 的处理方式与其他通用寄存器并不完全相同，因此设计 RESTORE_ALL 时不能简单地把所有寄存器按照完全相同的方式恢复。

对于 RESTORE_ALL 而言，对外表现为“输入当前 Trap frame，输出恢复后的处理器执行状态”

\_\_alltraps

需要负责建立汇编 Trap 入口与 C 语言 trap(struct trapframe \*tf) 之间的调用关系，使保存后的 Trap frame 能够按照 RISC-V 调用约定正确传递给 trap()；这个模块完成后，系统应能够形成完整的“Trap 进入 -> 上下文保存 -> C 语言处理 -> 上下文恢复 -> Trap 返回”闭环。

需要保证当前 Trap frame 的地址能够按照 RISC-V 调用约定作为 trap() 的参数传递，同时不能破坏后续恢复上下文所需要的信息。

对于 \_\_alltraps 而言，对外表现为“将 Trap 上下文正确传递给 C 语言 Trap 处理函数”；

**Requirements：**

- 整个模块不得修改 SAVE_ALL、struct trapframe 和 trap() 的函数签名。
- 需要注释的部分要保持注释简洁
- 工作完成后，你需要分析Makefile，并给我基于当前功能可用的测试方案（这并不意味着你可以修改现有测试或生成测试）。

## 2412143 林家熠

```markdown
[PROMPT]
修改文件 kern/trap/trap.c，实现 exception_handler(struct trapframe \*tf) 函数。当发生异常时，根据 tf->cause 进行分类处理，并正确更新 tf->epc，确保 sret 返回后不会重复触发异常。

[RELY]

1. 结构体：struct trapframe \*tf（包含 cause, epc）。
2. 宏定义：CAUSE_ILLEGAL_INSTRUCTION, CAUSE_BREAKPOINT。
3. 已有函数：cprintf(), print_trapframe(tf)。

[GUARANTEE]
必须实现或修改的接口：
void exception_handler(struct trapframe \*tf);

[SPECIFICATION]
**Pre-Condition**:

- 硬件已触发异常，SAVE_ALL 宏已将上下文保存至 tf 中，tf->cause 和 tf->epc 已就绪。
  **Post-Condition**:

1. 若 tf->cause == CAUSE_ILLEGAL_INSTRUCTION：
   - 输出非法指令异常提示及当前 epc。
   - 根据指令长度更新 tf->epc（若指令低 2 位为 0x3，则 tf->epc += 4；否则 tf->epc += 2）。
2. 若 tf->cause == CAUSE_BREAKPOINT：
   - 输出断点异常提示及当前 epc。
   - 根据指令长度更新 tf->epc（规则同上）。
3. 对于其他异常：
   - 调用 print_trapframe(tf) 进行兜底输出。
     **Requirements**:

- 严禁修改函数签名。
- 必须严格处理 tf->epc，防止陷入异常死循环。
```

## 2412934 王海沛
```markdown
[PROMPT]

请完成 RISC-V Lab3 的中断处理模块。先检查实验框架的真实定义，
在 OS-lab3/code/kern/trap/trap.c 的 lab3 function2 填空区域中，
仅实现我负责的 interrupt_handler(struct trapframe \*tf)。
保留 lab3 注释，把本人负责区域的 YOUR CODE 替换为 2412934。
不得修改已有其他函数，不实现 exception_handler，不完成 challenge。
需要时可在该区域添加头文件引用、条件宏和函数声明。

[RELY]

可以依赖框架已有的 struct trapframe，其中 cause 和 epc 为 uintptr_t。
trap_dispatch 已根据 cause 最高位区分中断与异常。
使用 riscv.h 中实际存在的 IRQ_U/S/H/M_SOFT、IRQ_U/S/H/M_TIMER、
IRQ_U/S/H/M_EXT 宏进行分类，不自行编造中断编号。
可以调用 clock_set_next_event()、print_ticks()、print_trapframe(tf)、
cprintf() 和 libs/sbi.h 声明的 sbi_shutdown()。
clock 模块提供 volatile size_t ticks；trap.c 已定义 TICK_NUM 为 100。
若 SHUTDOWN_AFTER 已有定义则沿用；当前框架未定义时，使用
#ifndef 提供默认值 10，并在报告说明这是默认选择。
exception_handler 由其他成员实现；如需声明可以添加，但不能提供空实现。

[GUARANTEE]

交付函数：void interrupt_handler(struct trapframe \*tf);
实现中断类型区分、持续时钟事件、单次计数、周期统计和达到阈值后关机。
未知中断必须打印完整 Trap frame。
保持函数签名及现场返回语义，不修改 tf->epc 和 tf->cause。
交付范围限于本人填空区域，并提供真实的验证记录。

[SPECIFICATION]

1. 输入由框架提供的有效 trapframe 指针。使用无符号运算清除
   cause 的最高位，在局部变量中取得中断原因码。
2. 对 IRQ_S_TIMER，先调用 clock_set_next_event()，再使 ticks
   恰好增加一次，不能遗漏预约，也不能重复计数。
3. 当 ticks % TICK_NUM == 0 时，调用已有 print_ticks()，
   保持框架的“100 ticks”输出格式，不另写一套统计输出。
4. 用静态计数记录实际调用统计输出的次数；达到 SHUTDOWN_AFTER
   时调用 sbi_shutdown()。默认阈值下预期第 1000 次中断关机。
5. 其他已列出的中断输出对应类型提示，例如 IRQ_S_SOFT 输出
   “Supervisor software interrupt”，各分支不得意外贯穿。
6. 未匹配原因调用 print_trapframe(tf)，不实现额外恢复逻辑。
7. 异步中断处理不增加 epc，正常路径返回统一 Trap 返回流程。
8. 使用项目交叉编译器检查语法和声明。只有实际执行并成功的检查
   才能标记为通过，不把语法检查等同于完整构建或 QEMU 测试。
```
