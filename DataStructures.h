//===- DataStructures.h - IRQ Analysis Data Structures ------------------===//
//
// 中断处理函数分析器的数据结构定义
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_DATA_STRUCTURES_H
#define IRQ_ANALYSIS_DATA_STRUCTURES_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include <vector>
#include <string>
#include <set>
#include <map>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 指针链追踪数据结构
//===----------------------------------------------------------------------===//

struct PointerChainElement {
    enum ElementType {
        GLOBAL_VAR_BASE,      // 全局变量作为起点
        IRQ_HANDLER_ARG0,     // 中断处理函数参数0 (int irq)
        IRQ_HANDLER_ARG1,     // 中断处理函数参数1 (void *dev_id)
        STRUCT_FIELD_DEREF,   // 结构体字段解引用
        ARRAY_INDEX_DEREF,    // 数组索引解引用
        DIRECT_LOAD,          // 直接load指令
        CONSTANT_OFFSET       // 常量偏移
    };
    
    ElementType type;
    std::string symbol_name;      // 符号名（如果有）
    std::string struct_type_name; // 结构体类型名
    int64_t offset;              // 字段偏移或数组索引
    Value *llvm_value;           // 对应的LLVM Value
    
    PointerChainElement() : type(DIRECT_LOAD), offset(0), llvm_value(nullptr) {}
};

struct PointerChain {
    std::vector<PointerChainElement> elements;
    int confidence;              // 整个链的置信度
    bool is_complete;           // 是否成功追踪到根源
    
    PointerChain() : confidence(0), is_complete(false) {}
    
    std::string toString() const;
};

//===----------------------------------------------------------------------===//
// 内存访问分析数据结构
//===----------------------------------------------------------------------===//

struct MemoryAccessInfo {
    enum AccessType {
        GLOBAL_VARIABLE,
        STRUCT_FIELD_ACCESS,
        ARRAY_ELEMENT,
        IRQ_HANDLER_DEV_ID_ACCESS,    // 通过dev_id参数访问
        IRQ_HANDLER_IRQ_ACCESS,       // 通过irq参数访问
        CONSTANT_ADDRESS,
        INDIRECT_ACCESS,
        POINTER_CHAIN_ACCESS          // 指针链访问
    };
    
    AccessType type;
    std::string symbol_name;      // 符号名（如果可解析）
    std::string struct_type_name; // 结构体类型名
    int64_t offset;               // 偏移量
    unsigned access_size;         // 访问大小（字节）
    bool is_write;                // 是否为写访问
    bool is_atomic;               // 是否为原子操作
    int confidence;               // 置信度 0-100
    std::string source_location;  // 源码位置
    
    // 指针链信息
    PointerChain pointer_chain;   // 完整的指针访问链
    std::string chain_description; // 人类可读的链描述
    
    MemoryAccessInfo() : type(INDIRECT_ACCESS), offset(0), access_size(0), 
                        is_write(false), is_atomic(false), confidence(0) {}
    
    // 实用函数
    bool isDeviceRelatedAccess() const;
    bool isHighConfidenceAccess() const;
    bool isWriteAccess() const;
    std::string getFuzzingTargetDescription() const;
};

//===----------------------------------------------------------------------===//
// 用于调试和输出的字符串转换函数
//===----------------------------------------------------------------------===//

// 现在可以使用完整的类型定义
const char* getAccessTypeName(MemoryAccessInfo::AccessType type);
const char* getPointerChainElementTypeName(PointerChainElement::ElementType type);

// 重载版本，接受int类型（用于JSON输出）
const char* getAccessTypeName(int type);
const char* getPointerChainElementTypeName(int type);

//===----------------------------------------------------------------------===//
// 寄存器访问分析数据结构
//===----------------------------------------------------------------------===//

struct RegisterAccessInfo {
    std::string register_name;
    bool is_write;
    std::string inline_asm_constraint;
    std::string source_location;
};

//===----------------------------------------------------------------------===//
// 函数指针分析数据结构
//===----------------------------------------------------------------------===//

struct FunctionPointerTarget {
    Function *target_function;
    int confidence;              // 0-100，表示调用此函数的可能性
    std::string analysis_reason; // 分析得出此目标的原因
    
    FunctionPointerTarget(Function *f, int conf, const std::string &reason) 
        : target_function(f), confidence(conf), analysis_reason(reason) {}
};

struct FunctionPointerAnalysis {
    Value *function_pointer;     // 函数指针Value
    std::string pointer_name;    // 指针名称或描述
    std::vector<FunctionPointerTarget> possible_targets; // 可能的目标函数
    bool is_resolved;           // 是否完全解析了所有可能目标
    
    FunctionPointerAnalysis() : function_pointer(nullptr), is_resolved(false) {}
};

struct IndirectCallAnalysis {
    CallInst *call_inst;        // 间接调用指令
    FunctionPointerAnalysis fp_analysis; // 对应的函数指针分析
    std::vector<MemoryAccessInfo> aggregated_accesses; // 聚合所有可能目标的内存访问
    std::vector<RegisterAccessInfo> aggregated_register_accesses; // 聚合寄存器访问
    
    // 实用函数
    size_t getTotalPossibleTargets() const;
    size_t getHighConfidenceTargets() const;
    std::vector<Function*> getMostLikelyTargets(int min_confidence = 70) const;
};

//===----------------------------------------------------------------------===//
// 函数调用分析数据结构
//===----------------------------------------------------------------------===//

struct FunctionCallInfo {
    std::string callee_name;
    bool is_direct_call;
    bool is_kernel_function;
    std::vector<std::string> argument_types;
    std::string source_location;
    int confidence;              // 调用置信度（对间接调用）
    std::string analysis_reason; // 分析原因
    
    FunctionCallInfo() : is_direct_call(true), is_kernel_function(false), confidence(100) {}
};

//===----------------------------------------------------------------------===//
// 中断处理函数分析结果数据结构
//===----------------------------------------------------------------------===//

struct InterruptHandlerAnalysis {
    std::string function_name;
    std::string source_file;
    unsigned line_number;
    bool is_confirmed_irq_handler;
    
    // 内存访问分析
    std::vector<MemoryAccessInfo> memory_accesses;
    
    // 寄存器访问分析
    std::vector<RegisterAccessInfo> register_accesses;
    
    // 函数调用分析（包含间接调用的所有可能目标）
    std::vector<FunctionCallInfo> function_calls;
    
    // 间接调用分析
    std::vector<IndirectCallAnalysis> indirect_call_analyses;
    
    // 聚合的内存访问（包含间接调用的影响）
    std::vector<MemoryAccessInfo> total_memory_accesses;
    
    // 访问的数据结构类型
    std::set<std::string> accessed_struct_types;
    
    // 全局变量访问
    std::set<std::string> accessed_global_vars;
    
    // 控制流信息
    unsigned basic_block_count;
    unsigned loop_count;
    bool has_recursive_calls;
    
    InterruptHandlerAnalysis() : line_number(0), is_confirmed_irq_handler(false),
                               basic_block_count(0), loop_count(0), 
                               has_recursive_calls(false) {}
};

#endif // IRQ_ANALYSIS_DATA_STRUCTURES_H
