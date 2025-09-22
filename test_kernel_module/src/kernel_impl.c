// kernel_impl.c - 简化的内核函数实现
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../include/kernel_compat.h"

// 全局变量
unsigned long jiffies = 0;

// 内存管理函数
void *kmalloc(size_t size, unsigned int flags)
{
    return malloc(size);
}

void kfree(const void *ptr)
{
    free((void *)ptr);
}

// 字符串函数
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    int ret;
    
    va_start(args, fmt);
    ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    
    return ret;
}

// 中断相关 - 空实现
int request_irq(unsigned int irq, irqreturn_t (*handler)(int, void *),
                unsigned long irqflags, const char *devname, void *dev_id)
{
    return 0;  // 成功
}

void free_irq(unsigned int irq, void *dev_id)
{
    // 空实现
}

// 工作队列相关 - 空实现
struct workqueue_struct *create_singlethread_workqueue(const char *name)
{
    struct workqueue_struct *wq = malloc(sizeof(struct workqueue_struct));
    if (wq) {
        snprintf(wq->name, sizeof(wq->name), "%s", name);
    }
    return wq;
}

void destroy_workqueue(struct workqueue_struct *wq)
{
    if (wq) {
        free(wq);
    }
}

int queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
    return 1;  // 成功
}
