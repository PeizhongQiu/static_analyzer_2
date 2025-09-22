#ifndef TEST_STRUCTURES_H
#define TEST_STRUCTURES_H

#include "kernel_compat.h"

// 设备状态枚举
enum device_state {
    DEV_STATE_IDLE = 0,
    DEV_STATE_ACTIVE = 1,
    DEV_STATE_ERROR = 2,
    DEV_STATE_RESET = 3
};

// 中断统计结构
struct irq_stats {
    unsigned long total_irqs;
    unsigned long error_irqs;
    unsigned long spurious_irqs;
    unsigned int last_error_code;
};

// 缓冲区管理结构
struct buffer_info {
    void *data_ptr;
    size_t size;
    size_t used;
    struct buffer_info *next;
    atomic_t ref_count;
};

// 设备寄存器映射
struct device_regs {
    volatile uint32_t control;
    volatile uint32_t status;
    volatile uint32_t data;
    volatile uint32_t irq_mask;
    volatile uint32_t dma_addr;
};

// 主设备结构
struct test_device {
    struct device_regs *regs;
    struct irq_stats stats;
    struct buffer_info *rx_buffers;
    struct buffer_info *tx_buffers;
    spinlock_t lock;
    struct list_head device_list;
    enum device_state state;
    int irq_number;
    void (*callback)(struct test_device *dev, int reason);
    struct work_struct work;
    char name[32];
    unsigned long flags;
};

// 函数指针类型定义
typedef int (*process_func_t)(void *data, size_t len);

// 全局变量声明
extern struct test_device *global_devices[8];
extern struct list_head global_device_list;
extern spinlock_t global_lock;
extern unsigned long global_irq_count;
extern unsigned long global_error_count;
extern atomic_t active_devices;
extern process_func_t process_handlers[4];
extern size_t default_buffer_size;

// 函数声明
int device_init(struct test_device *dev, int irq);
void device_cleanup(struct test_device *dev);
struct buffer_info *alloc_buffer(size_t size);
void free_buffer(struct buffer_info *buf);
void register_process_handlers(void);

// 中断处理函数声明
irqreturn_t test_irq_handler(int irq, void *dev_id);
irqreturn_t handle_rx_interrupt(struct test_device *dev);
irqreturn_t handle_tx_interrupt(struct test_device *dev);
irqreturn_t handle_error_interrupt(struct test_device *dev, uint32_t status);

// 复杂操作函数声明
int complex_buffer_operations(struct buffer_info *buf1, struct buffer_info *buf2);
int complex_device_operations(struct test_device *dev);

#endif // TEST_STRUCTURES_H
