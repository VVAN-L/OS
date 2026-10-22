#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

// 初始化 SLUB 分配器
void slub_init(void);

// 分配任意大小的内存 (最大支持 512 字节)
void *kmalloc(size_t size);

// 释放内存
void kfree(void *ptr);

// 测试函数
void slub_check(void);

#endif /* !__KERN_MM_SLUB_H__ */