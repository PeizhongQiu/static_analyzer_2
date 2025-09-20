//===- IRQHandlerIdentifier.h - Interrupt Handler Identifier (修复版) ----===//
//
// 从handler.json文件读取中断处理函数列表，支持静态函数
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_IRQ_HANDLER_IDENTIFIER_H
#define IRQ_ANALYSIS_IRQ_HANDLER_IDENTIFIER_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include <set>
#include <string>
#include <vector>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 中断处理函数识别器
//===----------------------------------------------------------------------===//

class InterruptHandlerIdentifier {
private:
    std::vector<std::string> handler_names;
    std::set<Function*> identified_handlers;
    size_t total_entries = 0;
    size_t duplicate_count = 0;
    
    /// 在模块中查找指定名称的函数（支持静态函数）
    Function* findFunctionByName(Module &M, const std::string& func_name);
    
public:
    /// 解析handler.json文件 - 公共接口
    bool parseHandlerJsonFile(const std::string& json_file);
    
    /// 从handler.json文件加载并识别处理函数
    bool loadHandlersFromJson(const std::string& json_file, Module &M);
    
    /// 获取识别出的中断处理函数集合
    const std::set<Function*>& getIdentifiedHandlers() const {
        return identified_handlers;
    }
    
    /// 检查指定函数是否被识别为中断处理函数
    bool isIdentifiedHandler(Function *F) const {
        return identified_handlers.find(F) != identified_handlers.end();
    }
    
    /// 获取识别出的处理函数数量
    size_t getHandlerCount() const { return identified_handlers.size(); }
    
    /// 获取加载的处理函数名称列表
    const std::vector<std::string>& getHandlerNames() const { return handler_names; }
    
    /// 获取处理函数总数（去重前）
    size_t getTotalHandlerEntries() const { return total_entries; }
    
    /// 获取重复的处理函数数量
    size_t getDuplicateCount() const { return duplicate_count; }
    
    /// 检查是否有重复的处理函数
    bool hasDuplicates() const { return duplicate_count > 0; }
};

#endif // IRQ_ANALYSIS_IRQ_HANDLER_IDENTIFIER_H
