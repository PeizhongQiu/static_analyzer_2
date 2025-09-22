#include "../include/test_structures.h"

// 静态处理计数器和数组
static atomic_t process_count = { .counter = 0 };
static uint32_t lookup_table[256];
static uint32_t processing_buffer[1024];
static int table_initialized = 0;

// 初始化查找表 - 重要的静态数组初始化
static void init_lookup_table(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        lookup_table[i] = i ^ (i << 8) ^ (i << 16) ^ (i << 24);
    }
    table_initialized = 1;
}

// 数据处理函数1：复杂过滤器 - 重要的分析目标
int simple_filter_process(void *data, size_t len)
{
    uint8_t *bytes = (uint8_t *)data;
    uint32_t *words = (uint32_t *)data;
    size_t i, word_count = len / sizeof(uint32_t);
    uint32_t accumulator = 0;
    
    if (!data || len == 0)
        return -1;
    
    if (!table_initialized)
        init_lookup_table();
    
    atomic_inc(&process_count);
    
    // 字节级处理 - 复杂的数组操作
    for (i = 0; i < len && i < 1024; i++) {
        uint8_t original = bytes[i];
        bytes[i] = lookup_table[original] & 0xFF;
        accumulator += bytes[i];
        
        // 写入处理缓冲区
        if (i < sizeof(processing_buffer)) {
            ((uint8_t *)processing_buffer)[i] = bytes[i];
        }
    }
    
    // 字级处理 - 复杂的指针和数组操作
    for (i = 0; i < word_count && i < 256; i++) {
        uint32_t original = words[i];
        words[i] = words[i] ^ lookup_table[i % 256] ^ accumulator;
        
        // 交叉引用处理
        if (i > 0) {
            words[i] ^= words[i-1];
        }
        
        // 更新处理缓冲区
        processing_buffer[i] = words[i];
    }
    
    // 全局统计更新
    global_irq_count += accumulator;
    
    return len;
}

// 数据处理函数2：复杂变换 - 重要的分析目标
int complex_transform_process(void *data, size_t len)
{
    uint32_t *words = (uint32_t *)data;
    size_t word_count = len / sizeof(uint32_t);
    size_t i, j;
    static uint32_t state_machine[8] = {
        0x12345678, 0x9ABCDEF0, 0xFEDCBA98, 0x76543210,
        0x13579BDF, 0x2468ACE0, 0xAAAAAAAA, 0x55555555
    };
    uint32_t temp_array[128];
    
    if (!data || len < sizeof(uint32_t))
        return -1;
    
    atomic_inc(&process_count);
    
    // 限制处理大小
    if (word_count > 128)
        word_count = 128;
    
    // 复制到临时数组 - 重要的内存操作
    for (i = 0; i < word_count; i++) {
        temp_array[i] = words[i];
    }
    
    // 多阶段变换 - 复杂的数组和状态机操作
    for (i = 0; i < word_count; i++) {
        // 第一阶段：与状态机异或
        temp_array[i] ^= state_machine[i % 8];
        
        // 第二阶段：位旋转
        temp_array[i] = (temp_array[i] << 8) | (temp_array[i] >> 24);
        
        // 第三阶段：与前后值混合
        if (i > 0) {
            temp_array[i] ^= temp_array[i-1];
        }
        if (i < word_count - 1) {
            temp_array[i] ^= words[i+1];  // 使用原始数据
        }
        
        // 更新状态机 - 重要的静态变量修改
        state_machine[i % 8] = temp_array[i];
    }
    
    // 反向处理验证 - 复杂的循环和数组操作
    for (j = word_count; j > 0; j--) {
        i = j - 1;
        uint32_t backup = temp_array[i];
        
        // 反向变换
        temp_array[i] ^= (i > 0) ? temp_array[i-1] : 0;
        temp_array[i] = (temp_array[i] >> 8) | (temp_array[i] << 24);
        
        // 写入处理缓冲区
        if (i < 1024) {
            processing_buffer[i] = backup ^ temp_array[i];
        }
    }
    
    // 写回结果 - 重要的内存写操作
    for (i = 0; i < word_count; i++) {
        words[i] = temp_array[i];
    }
    
    return word_count * sizeof(uint32_t);
}

// 数据处理函数3：统计分析 - 重要的分析目标
int statistics_process(void *data, size_t len)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t i;
    static uint32_t histogram[256] = {0};  // 静态直方图数组
    static uint64_t total_bytes = 0;       // 静态累计计数
    static uint32_t rolling_average[32] = {0};  // 滚动平均数组
    static int avg_index = 0;
    uint32_t sum = 0, min_val = 255, max_val = 0;
    
    if (!data || len == 0)
        return -1;
    
    atomic_inc(&process_count);
    
    // 统计信息收集 - 复杂的数组操作
    for (i = 0; i < len; i++) {
        uint8_t val = bytes[i];
        
        // 更新直方图 - 重要的静态数组修改
        histogram[val]++;
        sum += val;
        
        // 更新最值
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
        
        // 更新处理缓冲区
        if (i < sizeof(processing_buffer)) {
            ((uint8_t *)processing_buffer)[i] = val;
        }
    }
    
    total_bytes += len;
    
    // 更新滚动平均 - 重要的静态数组操作
    rolling_average[avg_index] = sum / len;
    avg_index = (avg_index + 1) % 32;
    
    // 计算总平均值
    uint32_t total_avg = 0;
    for (i = 0; i < 32; i++) {
        total_avg += rolling_average[i];
    }
    total_avg /= 32;
    
    // 将统计信息写入数据开头 - 重要的内存写操作
    if (len >= 16) {
        uint32_t *stats = (uint32_t *)data;
        stats[0] = sum;
        stats[1] = (min_val << 16) | max_val;
        stats[2] = total_avg;
        stats[3] = (uint32_t)(total_bytes & 0xFFFFFFFF);
    }
    
    return len;
}

// 注册处理函数 - 重要的函数指针操作
void register_process_handlers(void)
{
    process_handlers[0] = simple_filter_process;
    process_handlers[1] = complex_transform_process;
    process_handlers[2] = statistics_process;
    process_handlers[3] = NULL;  // 保留空位
}

// 获取处理统计信息
int get_process_stats(void)
{
    return atomic_read(&process_count);
}
