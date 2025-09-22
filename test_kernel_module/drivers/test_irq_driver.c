#include "../include/test_structures.h"

// 模块参数
static int test_irq = 42;

// 静态设备实例和工作队列
static struct test_device *main_device = NULL;
static struct workqueue_struct *test_workqueue = NULL;

// 工作队列处理函数
static void test_work_handler(struct work_struct *work)
{
    struct test_device *dev = container_of(work, struct test_device, work);
    unsigned long flags = 0;
    struct buffer_info *buf;
    static int work_counter = 0;
    
    if (!dev)
        return;
    
    work_counter++;
    
    spin_lock_irqsave(&dev->lock, flags);
    
    // 处理接收缓冲区 - 复杂的指针和缓冲区操作
    buf = dev->rx_buffers;
    while (buf) {
        if (buf->used > 0 && process_handlers[1]) {
            // 函数指针调用 - 重要的间接调用
            int result = process_handlers[1](buf->data_ptr, buf->used);
            if (result > 0) {
                buf->used = result;
            }
        }
        buf = buf->next;  // 链表遍历
    }
    
    // 更新设备状态 - 重要的状态机操作
    switch (dev->state) {
    case DEV_STATE_ERROR:
        dev->state = DEV_STATE_ACTIVE;
        dev->regs->control |= 0x01;  // 重新启用设备
        break;
    case DEV_STATE_RESET:
        dev->state = DEV_STATE_IDLE;
        dev->regs->status = 0x01;
        break;
    default:
        break;
    }
    
    // 更新工作统计
    dev->flags |= (work_counter << 8);
    
    spin_unlock_irqrestore(&dev->lock, flags);
}

// 设备回调函数 - 重要的函数指针目标
static void device_callback(struct test_device *dev, int reason)
{
    static unsigned long callback_count = 0;
    static uint32_t callback_history[64];
    
    callback_count++;
    
    // 记录回调历史 - 静态数组操作
    callback_history[callback_count % 64] = reason | (callback_count << 16);
    
    // 根据不同原因执行不同操作 - 复杂的条件处理
    switch (reason & 0x07) {
    case 0x01:  // RX中断
        if (test_workqueue) {
            // 调度工作队列 - 重要的工作队列操作
            queue_work(test_workqueue, &dev->work);
        }
        break;
        
    case 0x02:  // TX完成
        if (dev->tx_buffers && dev->tx_buffers->used == 0) {
            // 准备新的发送数据 - 复杂的缓冲区操作
            struct buffer_info *buf = dev->tx_buffers;
            uint32_t *data = (uint32_t *)buf->data_ptr;
            int i;
            
            for (i = 0; i < 16 && i * sizeof(uint32_t) < buf->size; i++) {
                data[i] = callback_count + i + callback_history[i % 64];
            }
            buf->used = min(16 * sizeof(uint32_t), buf->size);
        }
        break;
        
    case 0x04:  // 错误
        dev->flags |= (1 << 0);  // 设置错误标志
        global_error_count++;   // 全局变量修改
        break;
        
    default:
        // 默认处理
        dev->flags |= (reason << 4);
        break;
    }
}

// 模块初始化
static int test_irq_driver_init(void)
{
    int ret;
    
    // 创建工作队列
    test_workqueue = create_singlethread_workqueue("test_irq_wq");
    if (!test_workqueue) {
        return -ENOMEM;
    }
    
    // 分配设备结构
    main_device = kmalloc(sizeof(struct test_device), GFP_KERNEL);
    if (!main_device) {
        destroy_workqueue(test_workqueue);
        return -ENOMEM;
    }
    
    // 初始化设备
    ret = device_init(main_device, test_irq);
    if (ret < 0) {
        kfree(main_device);
        destroy_workqueue(test_workqueue);
        return ret;
    }
    
    // 设置设备回调和工作队列
    main_device->callback = device_callback;  // 函数指针赋值
    INIT_WORK(&main_device->work, test_work_handler);
    
    // 注册处理函数 - 重要的函数指针注册
    register_process_handlers();
    
    // 注册中断处理函数
    ret = request_irq(test_irq, test_irq_handler, IRQF_SHARED, 
                      "test_irq", main_device);
    if (ret < 0) {
        device_cleanup(main_device);
        kfree(main_device);
        destroy_workqueue(test_workqueue);
        return ret;
    }
    
    return 0;
}

// 模块清理
static void test_irq_driver_exit(void)
{
    if (main_device) {
        free_irq(test_irq, main_device);
        device_cleanup(main_device);
        kfree(main_device);
    }
    
    if (test_workqueue) {
        destroy_workqueue(test_workqueue);
    }
}

// 模块定义 - 简化版
int init_module(void) { return test_irq_driver_init(); }
void cleanup_module(void) { test_irq_driver_exit(); }
