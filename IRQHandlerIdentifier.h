//===- IRQHandlerIdentifier.h - Interrupt Handler Identifier ------------===//
//
// 从handler.json文件读取中断处理函数列表
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_HANDLER_IDENTIFIER_H
#define IRQ_HANDLER_IDENTIFIER_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include <set>
#include <string>
#include <vector>
#include <map>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 处理函数组合结构
//===----------------------------------------------------------------------===//

struct HandlerCombination {
    std::string handler;      // 中断处理函数名
    std::string thread_fn;    // 线程化处理函数名（可选）
    
    HandlerCombination() = default;
    HandlerCombination(const std::string& h, const std::string& t = "") 
        : handler(h), thread_fn(t) {}
};

//===----------------------------------------------------------------------===//
// 中断处理函数识别器
//===----------------------------------------------------------------------===//

class InterruptHandlerIdentifier {
private:
    std::vector<std::string> handler_names;
    std::vector<HandlerCombination> combinations;
    std::set<Function*> identified_handlers;
    
    // 统计信息
    size_t total_entries;
    size_t duplicate_count;
    
public:
    InterruptHandlerIdentifier() : total_entries(0), duplicate_count(0) {}
    
    /// 解析handler.json文件
    bool parseHandlerJsonFile(const std::string& json_file);
    
    /// 从handler.json文件加载并在模块中识别处理函数
    bool loadHandlersFromJson(const std::string& json_file, Module &M);
    
    /// 在多个模块中查找处理函数
    std::map<std::string, Function*> findHandlersInModules(
        const std::vector<std::unique_ptr<Module>>& modules);
    
    /// 获取处理函数名称列表
    const std::vector<std::string>& getHandlerNames() const { 
        return handler_names; 
    }
    
    /// 获取处理函数组合列表
    const std::vector<HandlerCombination>& getCombinations() const {
        return combinations;
    }
    
    /// 获取识别出的中断处理函数集合
    const std::set<Function*>& getIdentifiedHandlers() const {
        return identified_handlers;
    }
    
    /// 检查指定函数是否被识别为中断处理函数
    bool isIdentifiedHandler(Function *F) const {
        return identified_handlers.find(F) != identified_handlers.end();
    }
    
    /// 获取识别出的处理函数数量
    size_t getHandlerCount() const { 
        return identified_handlers.size(); 
    }
    
    /// 获取处理函数总数（去重前）
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
        identified_handlers.clear();
        total_entries = 0;
        duplicate_count = 0;
    }
    
    /// 打印统计信息
    void printStatistics() const;

private:
    /// 在单个模块中查找指定名称的函数
    Function* findFunctionByName(Module &M, const std::string& func_name);
    
    /// 验证函数是否符合中断处理函数的特征
    bool validateInterruptHandler(Function *F) const;
    
    /// 添加处理函数名称（去重）
    void addHandlerName(const std::string& name);
};

#endif // IRQ_HANDLER_IDENTIFIER_H
