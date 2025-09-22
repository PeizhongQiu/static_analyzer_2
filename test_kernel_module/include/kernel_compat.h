#ifndef KERNEL_COMPAT_H
#define KERNEL_COMPAT_H

#include <stdint.h>  // Use standard integer types
#include <stddef.h>  // For size_t and NULL

// Only define types that aren't in standard headers
typedef unsigned long size_t;
typedef long ssize_t;

// IRQ 返回值
typedef int irqreturn_t;
#define IRQ_NONE      (0)
#define IRQ_HANDLED   (1)

// GFP 标志 (简化版)
#define GFP_KERNEL    0x01
#define GFP_ATOMIC    0x02

// 原子操作结构
typedef struct {
    volatile int counter;
} atomic_t;

// 自旋锁结构
typedef struct {
    volatile unsigned int lock;
} spinlock_t;

// 链表结构
struct list_head {
    struct list_head *next, *prev;
};

// 工作队列结构
struct work_struct {
    unsigned long data;
    void (*func)(struct work_struct *work);
};

struct workqueue_struct {
    char name[16];
};

// 内存屏障宏 (空实现)
#define __iomem
#ifndef volatile
#define volatile volatile
#endif

// 错误码
#define ENOMEM    12
#define EINVAL    22

// 常用宏
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// 简化的内核函数声明
void *kmalloc(size_t size, unsigned int flags);
void kfree(const void *ptr);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int snprintf(char *buf, size_t size, const char *fmt, ...);

// 原子操作函数
static inline void atomic_set(atomic_t *v, int i) { v->counter = i; }
static inline int atomic_read(atomic_t *v) { return v->counter; }
static inline void atomic_inc(atomic_t *v) { v->counter++; }
static inline void atomic_dec(atomic_t *v) { v->counter--; }
static inline int atomic_dec_and_test(atomic_t *v) { return --v->counter == 0; }

// 自旋锁操作
static inline void spin_lock_init(spinlock_t *lock) { lock->lock = 0; }
static inline void spin_lock(spinlock_t *lock) { lock->lock = 1; }
static inline void spin_unlock(spinlock_t *lock) { lock->lock = 0; }
static inline void spin_lock_irqsave(spinlock_t *lock, unsigned long flags) { lock->lock = 1; }
static inline void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags) { lock->lock = 0; }

// 链表操作
static inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

static inline void list_add(struct list_head *new_entry, struct list_head *head) {
    new_entry->next = head->next;
    new_entry->prev = head;
    head->next->prev = new_entry;
    head->next = new_entry;
}

static inline void list_del(struct list_head *entry) {
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
}

// 工作队列操作
static inline void INIT_WORK(struct work_struct *work, void (*func)(struct work_struct *)) {
    work->func = func;
}

// 中断相关
int request_irq(unsigned int irq, irqreturn_t (*handler)(int, void *),
                unsigned long irqflags, const char *devname, void *dev_id);
void free_irq(unsigned int irq, void *dev_id);

// 工作队列相关
struct workqueue_struct *create_singlethread_workqueue(const char *name);
void destroy_workqueue(struct workqueue_struct *wq);
int queue_work(struct workqueue_struct *wq, struct work_struct *work);

// 模块相关
#define module_param(name, type, perm)
#define MODULE_PARM_DESC(var, desc)
#define module_init(fn) int init_module(void) { return fn(); }
#define module_exit(fn) void cleanup_module(void) { fn(); }
#define MODULE_LICENSE(license)
#define MODULE_AUTHOR(author)
#define MODULE_DESCRIPTION(desc)
#define MODULE_VERSION(version)

// 打印函数 (简化)
#define KERN_INFO     "<6>"
#define KERN_ERR      "<3>"
#define printk(fmt, args...) printf(fmt, ##args)

// 时间相关
extern unsigned long jiffies;

// 最小值宏
#define min(x, y) ((x) < (y) ? (x) : (y))

// 容器宏
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - (char *)&((type *)0)->member))

// IRQ 标志
#define IRQF_SHARED   0x00000080

// 静态定义宏
#define DEFINE_SPINLOCK(x) spinlock_t x = { .lock = 0 }
#define DEFINE_ATOMIC_T(x) atomic_t x = { .counter = 0 }
#define LIST_HEAD(name) struct list_head name = { &(name), &(name) }

#endif // KERNEL_COMPAT_H
