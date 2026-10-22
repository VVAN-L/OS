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
