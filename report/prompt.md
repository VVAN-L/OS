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

## RESTORE_ALL

使被打断程序的执行状态能够从 Trap frame 中恢复出来。

必须与已经提供的 SAVE_ALL 保持对应关系，不能自行假设一个与实际 Trap frame 不一致的数据布局。特别需要注意，SAVE_ALL 中对 x2(sp) 的处理方式与其他通用寄存器并不完全相同，因此设计 RESTORE_ALL 时不能简单地把所有寄存器按照完全相同的方式恢复。

对于 RESTORE_ALL 而言，对外表现为“输入当前 Trap frame，输出恢复后的处理器执行状态”

## \_\_alltraps

需要负责建立汇编 Trap 入口与 C 语言 trap(struct trapframe \*tf) 之间的调用关系，使保存后的 Trap frame 能够按照 RISC-V 调用约定正确传递给 trap()；这个模块完成后，系统应能够形成完整的“Trap 进入 -> 上下文保存 -> C 语言处理 -> 上下文恢复 -> Trap 返回”闭环。

需要保证当前 Trap frame 的地址能够按照 RISC-V 调用约定作为 trap() 的参数传递，同时不能破坏后续恢复上下文所需要的信息。

对于 \_\_alltraps 而言，对外表现为“将 Trap 上下文正确传递给 C 语言 Trap 处理函数”；

**Requirements：**

- 整个模块不得修改 SAVE_ALL、struct trapframe 和 trap() 的函数签名。
- 需要注释的部分要保持注释简洁
- 工作完成后，你需要分析Makefile，并给我基于当前功能可用的测试方案（这并不意味着你可以修改现有测试或生成测试）。
