#include "../include/test_structures.h"

// 设备初始化
int device_init(struct test_device *dev, int irq)
{
    int i;
    
    if (!dev)
        return -1;
    
    // 清零设备结构
    memset(dev, 0, sizeof(struct test_device));
    
    // 初始化基本字段
    dev->irq_number = irq;
    dev->state = DEV_STATE_IDLE;
    spin_lock_init(&dev->lock);
    INIT_LIST_HEAD(&dev->device_list);
    INIT_WORK(&dev->work, NULL);
    
    // 分配寄存器空间（模拟）
    dev->regs = kmalloc(sizeof(struct device_regs), GFP_KERNEL);
    if (!dev->regs)
        return -1;
    
    // 初始化寄存器 - 重要的内存写操作
    memset((void *)dev->regs, 0, sizeof(struct device_regs));
    dev->regs->control = 0x12345678;
    dev->regs->status = 0x00000001;
    dev->regs->irq_mask = 0xFFFFFFFF;
    dev->regs->dma_addr = 0x80000000;
    
    // 分配缓冲区 - 复杂的指针操作
    dev->rx_buffers = alloc_buffer(default_buffer_size);
    dev->tx_buffers = alloc_buffer(default_buffer_size);
    
    if (!dev->rx_buffers || !dev->tx_buffers) {
        if (dev->rx_buffers) free_buffer(dev->rx_buffers);
        if (dev->tx_buffers) free_buffer(dev->tx_buffers);
        kfree(dev->regs);
        return -1;
    }
    
    // 设置设备名称
    snprintf(dev->name, sizeof(dev->name), "test_device_%d", irq);
    
    // 添加到全局管理 - 重要的全局变量修改
    spin_lock(&global_lock);
    for (i = 0; i < 8; i++) {
        if (!global_devices[i]) {
            global_devices[i] = dev;
            break;
        }
    }
    list_add(&dev->device_list, &global_device_list);
    atomic_inc(&active_devices);
    spin_unlock(&global_lock);
    
    return 0;
}

// 设备清理
void device_cleanup(struct test_device *dev)
{
    int i;
    unsigned long flags = 0;
    
    if (!dev)
        return;
    
    // 从全局数组中移除 - 复杂的数据结构操作
    spin_lock_irqsave(&global_lock, flags);
    for (i = 0; i < 8; i++) {
        if (global_devices[i] == dev) {
            global_devices[i] = NULL;
            break;
        }
    }
    list_del(&dev->device_list);
    atomic_dec(&active_devices);
    spin_unlock_irqrestore(&global_lock, flags);
    
    // 释放资源 - 重要的内存操作
    if (dev->rx_buffers) {
        free_buffer(dev->rx_buffers);
        dev->rx_buffers = NULL;
    }
    
    if (dev->tx_buffers) {
        free_buffer(dev->tx_buffers);
        dev->tx_buffers = NULL;
    }
    
    if (dev->regs) {
        kfree(dev->regs);
        dev->regs = NULL;
    }
    
    dev->state = DEV_STATE_IDLE;
}

// 复杂的设备操作 - 重要的分析目标
int complex_device_operations(struct test_device *dev)
{
    uint32_t *reg_array;
    int operations = 0;
    static int operation_sequence = 0;
    static uint32_t state_history[32];
    
    if (!dev || !dev->regs)
        return -1;
    
    operation_sequence++;
    
    // 复杂的寄存器操作 - 多个内存写操作
    reg_array = (uint32_t *)dev->regs;
    reg_array[0] = operation_sequence;              // control寄存器
    reg_array[1] = dev->stats.total_irqs;          // status寄存器
    reg_array[2] = atomic_read(&active_devices);   // data寄存器
    reg_array[3] = 0xDEADBEEF;                     // irq_mask寄存器
    reg_array[4] = (uint32_t)(unsigned long)dev;  // dma_addr寄存器
    operations += 5;
    
    // 状态历史数组操作
    state_history[operation_sequence % 32] = reg_array[1];
    
    // 缓冲区链式操作
    if (dev->rx_buffers && dev->tx_buffers) {
        operations += complex_buffer_operations(dev->rx_buffers, dev->tx_buffers);
        
        // 缓冲区链接操作
        if (dev->rx_buffers->next == NULL) {
            struct buffer_info *new_buf = alloc_buffer(1024);
            if (new_buf) {
                dev->rx_buffers->next = new_buf;
                atomic_inc(&new_buf->ref_count);
            }
        }
    }
    
    // 更新全局统计 - 重要的全局变量修改
    dev->stats.total_irqs++;
    global_irq_count++;
    
    return operations;
}
