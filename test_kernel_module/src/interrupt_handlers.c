#include "../include/test_structures.h"

// 静态变量用于追踪中断状态
static unsigned long last_irq_jiffies[8] = {0};
static atomic_t total_handled_irqs = { .counter = 0 };
static uint32_t irq_state_machine[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};

// 主要的中断处理函数 - 这是我们要分析的主要目标
irqreturn_t test_irq_handler(int irq, void *dev_id)
{
    struct test_device *dev = (struct test_device *)dev_id;
    unsigned long flags = 0;
    uint32_t status, control;
    irqreturn_t ret = IRQ_NONE;
    static int call_count = 0;
    
    if (!dev || !dev->regs)
        return IRQ_NONE;
    
    call_count++;
    atomic_inc(&total_handled_irqs);
    global_irq_count++;
    
    spin_lock_irqsave(&dev->lock, flags);
    
    // 复杂的寄存器读写操作
    status = dev->regs->status;           // 读操作
    control = dev->regs->control;         // 读操作
    
    // 更新设备统计 - 多个内存写操作
    dev->stats.total_irqs++;
    dev->stats.last_error_code = status & 0xFFFF;
    last_irq_jiffies[irq % 8] = jiffies;
    
    // 状态机更新 - 静态数组操作
    irq_state_machine[call_count % 4] = status ^ control;
    
    // 检查中断类型并调用相应处理函数
    if (status & 0x01) {
        ret = handle_rx_interrupt(dev);
    }
    
    if (status & 0x02) {
        ret = handle_tx_interrupt(dev);
    }
    
    if (status & 0x04) {
        ret = handle_error_interrupt(dev, status);
    }
    
    // 清除中断状态 - 重要的寄存器写操作
    dev->regs->status = status;
    dev->regs->control = control | 0x80000000;  // 设置处理完成标志
    
    // 函数指针调用 - 重要的间接调用
    if (dev->callback) {
        dev->callback(dev, status);
    }
    
    // 执行复杂的设备操作
    complex_device_operations(dev);
    
    spin_unlock_irqrestore(&dev->lock, flags);
    
    return ret;
}

// 接收中断处理 - 重要的分析目标
irqreturn_t handle_rx_interrupt(struct test_device *dev)
{
    struct buffer_info *buf;
    uint32_t data, *buf_data;
    int i, processed = 0;
    static uint32_t rx_sequence = 0;
    
    if (!dev || !dev->rx_buffers)
        return IRQ_NONE;
    
    rx_sequence++;
    buf = dev->rx_buffers;
    buf_data = (uint32_t *)buf->data_ptr;
    
    // 从设备读取数据 - 复杂的内存操作
    for (i = 0; i < 16; i++) {
        if (dev->regs->status & 0x08) {  // 数据可用
            data = dev->regs->data;      // 寄存器读操作
            
            // 检查缓冲区空间
            if (buf->used + sizeof(uint32_t) <= buf->size) {
                // 复杂的数据处理
                uint32_t processed_data = data ^ rx_sequence ^ irq_state_machine[i % 4];
                buf_data[buf->used / sizeof(uint32_t)] = processed_data;
                buf->used += sizeof(uint32_t);
                processed++;
            } else {
                dev->stats.error_irqs++;
                break;
            }
        } else {
            break;
        }
    }
    
    // 函数指针调用 - 重要的间接调用
    if (process_handlers[0] && processed > 0) {
        process_handlers[0](buf->data_ptr, buf->used);
    }
    
    // 缓冲区链操作
    if (buf->next) {
        struct buffer_info *next_buf = buf->next;
        if (next_buf->used == 0) {
            // 切换到下一个缓冲区
            memcpy(next_buf->data_ptr, buf->data_ptr, min(buf->used, next_buf->size));
            next_buf->used = min(buf->used, next_buf->size);
            buf->used = 0;
        }
    }
    
    return IRQ_HANDLED;
}

// 发送中断处理 - 重要的分析目标
irqreturn_t handle_tx_interrupt(struct test_device *dev)
{
    struct buffer_info *buf;
    uint32_t *data_ptr;
    int i, transmitted = 0;
    static uint32_t tx_sequence = 0;
    
    if (!dev || !dev->tx_buffers)
        return IRQ_NONE;
    
    tx_sequence++;
    buf = dev->tx_buffers;
    data_ptr = (uint32_t *)buf->data_ptr;
    
    // 发送缓冲区中的数据 - 复杂的循环和指针操作
    for (i = 0; i < buf->used / sizeof(uint32_t); i++) {
        if (!(dev->regs->status & 0x10))  // 发送缓冲区满
            break;
        
        // 复杂的数据变换
        uint32_t send_data = data_ptr[i] ^ tx_sequence;
        dev->regs->data = send_data;      // 寄存器写操作
        transmitted++;
    }
    
    // 更新缓冲区使用状态 - 复杂的内存操作
    if (transmitted > 0) {
        size_t remaining = buf->used - (transmitted * sizeof(uint32_t));
        if (remaining > 0) {
            // 移动未发送的数据到缓冲区开头
            memmove(buf->data_ptr, &data_ptr[transmitted], remaining);
        }
        buf->used = remaining;
    }
    
    // 缓冲区链管理
    if (buf->used == 0 && buf->next) {
        // 切换到下一个缓冲区
        struct buffer_info *next_buf = buf->next;
        buf->next = next_buf->next;
        if (next_buf->used > 0) {
            memcpy(buf->data_ptr, next_buf->data_ptr, next_buf->used);
            buf->used = next_buf->used;
        }
        free_buffer(next_buf);
    }
    
    return IRQ_HANDLED;
}

// 错误中断处理 - 重要的分析目标
irqreturn_t handle_error_interrupt(struct test_device *dev, uint32_t status)
{
    unsigned int error_code = (status >> 16) & 0xFFFF;
    static uint32_t error_history[16];
    static int error_index = 0;
    
    // 更新错误统计 - 多个内存写操作
    dev->stats.error_irqs++;
    dev->stats.last_error_code = error_code;
    dev->stats.spurious_irqs += (error_code == 0) ? 1 : 0;
    global_error_count++;
    
    // 错误历史记录 - 静态数组操作
    error_history[error_index] = error_code | (status << 16);
    error_index = (error_index + 1) % 16;
    
    // 根据错误类型采取不同行动 - 复杂的条件分支
    switch (error_code) {
    case 0x0001:  // 缓冲区溢出
        if (dev->rx_buffers) {
            dev->rx_buffers->used = 0;       // 重置缓冲区
            memset(dev->rx_buffers->data_ptr, 0, 64);  // 清空数据
        }
        break;
        
    case 0x0002:  // DMA错误
        dev->state = DEV_STATE_ERROR;
        dev->regs->dma_addr = 0;             // 重置DMA地址
        dev->regs->control &= ~0x100;        // 禁用DMA
        break;
        
    case 0x0004:  // 校验和错误
        if (dev->tx_buffers && dev->tx_buffers->data_ptr) {
            // 重新计算校验和
            uint32_t *data = (uint32_t *)dev->tx_buffers->data_ptr;
            uint32_t checksum = 0;
            int i;
            
            for (i = 0; i < dev->tx_buffers->used / sizeof(uint32_t); i++) {
                checksum ^= data[i];
            }
            dev->regs->data = checksum;
        }
        break;
        
    default:
        // 未知错误，重置设备 - 多个寄存器写操作
        dev->regs->control = 0;
        dev->regs->status = 0;
        dev->regs->irq_mask = 0;
        dev->state = DEV_STATE_RESET;
        break;
    }
    
    return IRQ_HANDLED;
}
