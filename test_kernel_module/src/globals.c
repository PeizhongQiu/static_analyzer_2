#include "../include/test_structures.h"

// 全局设备管理
struct test_device *global_devices[8] = {NULL};
struct list_head global_device_list = { &global_device_list, &global_device_list };
spinlock_t global_lock = { .lock = 0 };

// 全局统计
unsigned long global_irq_count = 0;
unsigned long global_error_count = 0;
atomic_t active_devices = { .counter = 0 };

// 全局配置
size_t default_buffer_size = 4096;

// 全局函数指针数组
process_func_t process_handlers[4] = {NULL};
