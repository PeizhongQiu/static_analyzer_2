//===- IRQHandlerIdentifier.h - Interrupt Handler Identifier ------------===//
//
// 从handler.json文件读取中断处理函数列表
// 注意：只分析 'handler' 字段，忽略 'thread_fn' 字段
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_HANDLER_IDENTIFIER_H
#define IRQ_HANDLER_IDENTIFIER_H

#include <string>
#include <vector>

//===----------------------------------------------------------------------===//
// 处理函数组合结构
//===----------------------------------------------------------------------===//

struct HandlerCombination {
    std::string handler;      // 中断处理函数名（主要分析目标）
    std::string thread_fn;    // 线程化处理函数名（忽略，不分析）
    
    HandlerCombination() = default;
    HandlerCombination(const std::string& h, const std::string& t = "") 
        : handler(h), thread_fn(t) {}
};

//===----------------------------------------------------------------------===//
// 中断处理函数识别器
// 
// 设计原则：
// - 只分析 'handler' 字段指定的函数
// - 忽略 'thread_fn' 字段（这些是线程化的底半部处理，不是真正的中断处理函数）
// - 专注于硬件中断的顶半部处理函数
//===----------------------------------------------------------------------===//

class InterruptHandlerIdentifier {
private:
    std::vector<std::string> handler_names;        // 只包含handler函数名
    std::vector<HandlerCombination> combinations;  // 完整组合（但只使用handler部分）
    
    // 统计信息
    size_t total_entries;
    size_t duplicate_count;
    
public:
    InterruptHandlerIdentifier() : total_entries(0), duplicate_count(0) {}
    
    /// 解析handler.json文件（只提取handler字段）
    bool parseHandlerJsonFile(const std::string& json_file);
    
    /// 获取处理函数名称列表（只包含handler）
    const std::vector<std::string>& getHandlerNames() const { 
        return handler_names; 
    }
    
    /// 获取处理函数组合列表（包含完整信息但只使用handler）
    const std::vector<HandlerCombination>& getCombinations() const {
        return combinations;
    }
    
    /// 获取处理函数总数（去重前，只计算handler）
    size_t getTotalHandlerEntries() const { 
        return total_entries; 
    }
    
    /// 获取重复的处理函数数量
    size_t getDuplicateCount() const { 
        return duplicate_count; 
    }
    
    /// 检查是否有重复的处理函数
    bool hasDuplicates() const { 
        return duplicate_count > 0; 
    }
    
    /// 清空所有数据
    void clear() {
        handler_names.clear();
        combinations.clear();
        total_entries = 0;
        duplicate_count = 0;
    }
    
    /// 打印统计信息
    void printStatistics() const;

private:
    /// 添加处理函数名称（去重，只添加handler）
    void addHandlerName(const std::string& name);
};

#endif // IRQ_HANDLER_IDENTIFIER_H
