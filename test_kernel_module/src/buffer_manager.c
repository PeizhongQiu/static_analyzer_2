#include "../include/test_structures.h"

// 静态缓冲区池
static struct buffer_info *buffer_pool[16] = {NULL};
static spinlock_t pool_lock = { .lock = 0 };
static int pool_initialized = 0;

// 初始化缓冲区池
static void init_buffer_pool(void)
{
    int i;
    spin_lock_init(&pool_lock);
    for (i = 0; i < 16; i++) {
        buffer_pool[i] = NULL;
    }
    pool_initialized = 1;
}

// 分配缓冲区
struct buffer_info *alloc_buffer(size_t size)
{
    struct buffer_info *buf;
    int i;
    unsigned long flags = 0;
    
    if (!pool_initialized)
        init_buffer_pool();
    
    // 尝试从池中获取
    spin_lock_irqsave(&pool_lock, flags);
    for (i = 0; i < 16; i++) {
        if (buffer_pool[i] && buffer_pool[i]->size >= size) {
            buf = buffer_pool[i];
            buffer_pool[i] = NULL;
            spin_unlock_irqrestore(&pool_lock, flags);
            
            // 重置缓冲区
            buf->used = 0;
            atomic_set(&buf->ref_count, 1);
            memset(buf->data_ptr, 0, size);
            return buf;
        }
    }
    spin_unlock_irqrestore(&pool_lock, flags);
    
    // 分配新的缓冲区
    buf = kmalloc(sizeof(struct buffer_info), GFP_KERNEL);
    if (!buf)
        return NULL;
    
    buf->data_ptr = kmalloc(size, GFP_KERNEL);
    if (!buf->data_ptr) {
        kfree(buf);
        return NULL;
    }
    
    buf->size = size;
    buf->used = 0;
    buf->next = NULL;
    atomic_set(&buf->ref_count, 1);
    
    return buf;
}

// 释放缓冲区
void free_buffer(struct buffer_info *buf)
{
    int i;
    unsigned long flags = 0;
    
    if (!buf)
        return;
    
    // 减少引用计数
    if (!atomic_dec_and_test(&buf->ref_count))
        return;
    
    // 尝试放回池中
    spin_lock_irqsave(&pool_lock, flags);
    for (i = 0; i < 16; i++) {
        if (!buffer_pool[i]) {
            buffer_pool[i] = buf;
            spin_unlock_irqrestore(&pool_lock, flags);
            return;
        }
    }
    spin_unlock_irqrestore(&pool_lock, flags);
    
    // 池满了，直接释放
    kfree(buf->data_ptr);
    kfree(buf);
}

// 复杂的缓冲区操作 - 重要的分析目标
int complex_buffer_operations(struct buffer_info *buf1, struct buffer_info *buf2)
{
    uint32_t *data1, *data2;
    int i, operations = 0;
    static uint32_t operation_counter = 0;
    static uint32_t shared_array[64];
    
    if (!buf1 || !buf2 || !buf1->data_ptr || !buf2->data_ptr)
        return -1;
    
    operation_counter++;
    data1 = (uint32_t *)buf1->data_ptr;
    data2 = (uint32_t *)buf2->data_ptr;
    
    // 复杂的指针和数组操作
    for (i = 0; i < 64 && i * sizeof(uint32_t) < buf1->size; i++) {
        // 读取数据1
        uint32_t value1 = data1[i];
        
        // 写入共享数组
        shared_array[i] = value1 ^ operation_counter;
        
        // 修改原始数据
        data1[i] = shared_array[i] + i;
        operations++;
        
        // 跨缓冲区操作
        if (i * sizeof(uint32_t) < buf2->size) {
            data2[i] = data1[i] ^ shared_array[63 - i];
            operations++;
        }
    }
    
    // 更新缓冲区状态
    buf1->used = min(buf1->size, 64 * sizeof(uint32_t));
    buf2->used = min(buf2->size, 64 * sizeof(uint32_t));
    
    // 全局变量修改
    global_irq_count += operations;
    
    return operations;
}
