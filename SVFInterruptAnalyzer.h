//===- SVFInterruptAnalyzer.h - Enhanced SVF中断处理函数分析器 -----------===//
//
// 增强版SVF中断处理函数分析器
// 
// 核心功能:
// - 完整调用图构建 (函数和函数指针)
// - 基于调用图的全面内存操作分析
// - 递归数据结构分析
// - 全局/静态变量修改跟踪
//
//===----------------------------------------------------------------------===//

#ifndef SVF_INTERRUPT_ANALYZER_H
#define SVF_INTERRUPT_ANALYZER_H

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/JSON.h"

// SVF Headers - 条件编译
#ifdef SVF_AVAILABLE
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include "Graphs/VFG.h"
#include "Util/Options.h"
#endif

#include <memory>
#include <vector>
#include <string>
#include <map>
#include <set>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 数据结构定义
//===----------------------------------------------------------------------===//

/// 数据结构访问信息
struct DataStructureAccess {
    std::string struct_name;           // 结构体名称
    std::string field_name;            // 字段名称
    std::string access_pattern;        // 访问模式描述
    size_t offset;                     // 字段偏移
    std::string field_type;            // 字段类型
    bool is_pointer_field;             // 是否为指针字段
    
    DataStructureAccess() : offset(0), is_pointer_field(false) {}
};

/// 函数调用信息
struct FunctionCallInfo {
    std::string function_name;         // 函数名
    std::string call_type;             // 调用类型：direct, indirect, function_pointer
    size_t call_count;                 // 调用次数
    std::vector<std::string> call_sites; // 调用点信息
    std::vector<std::string> possible_targets; // 可能的目标（用于间接调用）
    
    FunctionCallInfo() : call_count(0) {}
};

/// 内存写操作信息
struct MemoryWriteOperation {
    std::string target_name;           // 写操作目标名称
    std::string target_type;           // 目标类型：global_var, static_var, struct_field
    std::string data_type;             // 数据类型
    size_t write_count;                // 写操作次数
    std::vector<std::string> write_locations; // 写操作位置
    bool is_critical;                  // 是否为关键写操作
    
    MemoryWriteOperation() : write_count(0), is_critical(false) {}
};

/// 完整调用图信息
struct CallGraphInfo {
    std::set<Function*> direct_functions;      // 直接调用的函数
    std::set<Function*> indirect_functions;    // 间接调用的函数
    std::set<Function*> all_functions;         // 所有函数的合集
    
    std::map<Function*, std::vector<std::string>> call_sites;  // 函数调用点
    std::map<std::string, std::vector<std::string>> function_pointers; // 函数指针映射
    std::vector<std::string> indirect_call_sites;              // 间接调用点
};

/// 分析结果数据结构
struct InterruptHandlerResult {
    // 基础信息
    std::string function_name;
    std::string source_file;
    std::string module_file;
    
    // 基础统计
    size_t total_instructions;
    size_t total_basic_blocks;
    size_t function_calls;
    size_t indirect_calls;
    size_t memory_read_operations;      // 分离读操作
    size_t memory_write_operations;     // 分离写操作
    
    // 增强的分析结果
    std::vector<DataStructureAccess> data_structure_accesses;  // 数据结构访问
    std::vector<FunctionCallInfo> function_call_details;       // 详细函数调用信息
    std::vector<MemoryWriteOperation> memory_writes;           // 内存写操作详情
    std::vector<std::string> modified_global_vars;             // 被修改的全局变量
    std::vector<std::string> modified_static_vars;             // 被修改的静态变量
    
    // SVF增强分析结果
    std::map<std::string, std::vector<std::string>> function_pointer_targets; // 函数指针目标
    std::vector<std::string> direct_function_calls;            // 直接函数调用
    std::vector<std::string> indirect_call_targets;            // 间接调用目标
    
    // 中断处理特征
    bool has_device_access;
    bool has_irq_operations;
    bool has_work_queue_ops;
    
    // 分析质量
    bool analysis_complete;
    double confidence_score;
    
    InterruptHandlerResult() : 
        total_instructions(0), total_basic_blocks(0), 
        function_calls(0), indirect_calls(0), 
        memory_read_operations(0), memory_write_operations(0),
        has_device_access(false), has_irq_operations(false), 
        has_work_queue_ops(false), analysis_complete(false), confidence_score(0.0) {}
};

//===----------------------------------------------------------------------===//
// 主分析器类
//===----------------------------------------------------------------------===//

class SVFInterruptAnalyzer {
private:
    // SVF核心组件
#ifdef SVF_AVAILABLE
    std::unique_ptr<SVF::SVFIR> svfir;
    std::unique_ptr<SVF::AndersenWaveDiff> pta;
    std::unique_ptr<SVF::VFG> vfg;
#endif
    
    // LLVM组件
    std::vector<std::unique_ptr<Module>> modules;
    LLVMContext* context;
    std::vector<std::string> loaded_bc_files;
    bool svf_initialized;
    
    // 分析缓存
    std::map<Type*, std::string> type_name_cache;
    std::map<std::string, std::vector<std::string>> struct_field_cache;

public:
    // 构造函数和析构函数
    explicit SVFInterruptAnalyzer(LLVMContext* ctx);
    ~SVFInterruptAnalyzer();
    
    // 禁用拷贝构造和赋值
    SVFInterruptAnalyzer(const SVFInterruptAnalyzer&) = delete;
    SVFInterruptAnalyzer& operator=(const SVFInterruptAnalyzer&) = delete;
    
    // 核心公共接口
    bool loadBitcodeFiles(const std::vector<std::string>& files);
    bool initializeSVF();
    std::vector<InterruptHandlerResult> analyzeInterruptHandlers(const std::vector<std::string>& handler_names);
    
    // 工具函数
    Function* findFunction(const std::string& name);
    size_t getModuleCount() const { return modules.size(); }
    
    // 输出和统计
    void outputResults(const std::vector<InterruptHandlerResult>& results, const std::string& output_file);
    void printStatistics() const;
    void printAnalysisBreakdown(const std::vector<InterruptHandlerResult>& results) const;

private:
    // ========================================================================
    // SVF核心功能 (SVFInterruptAnalyzer.cpp)
    // ========================================================================
    bool initializeSVFCore();
    bool runPointerAnalysis();
    bool buildVFG();
    void cleanupSVFResources();
    
    // ========================================================================
    // 重新设计的分析流程 (SVFInterruptAnalyzer.cpp)
    // ========================================================================
    InterruptHandlerResult analyzeSingleHandlerComplete(Function* handler);
    void printAnalysisSummary(const InterruptHandlerResult& result);
    
    // 完整调用图构建
    CallGraphInfo buildCompleteCallGraph(Function* root_function);
    void buildCallGraphRecursive(Function* function, CallGraphInfo& call_graph, 
                                std::set<Function*>& visited_functions, int depth);
    
    // 基于调用图的完整分析
    void analyzeMemoryOperationsComplete(const CallGraphInfo& call_graph, InterruptHandlerResult& result);
    void analyzeGlobalAndStaticWritesComplete(const CallGraphInfo& call_graph, InterruptHandlerResult& result);
    void analyzeDataStructuresComplete(const CallGraphInfo& call_graph, InterruptHandlerResult& result);
    void finalizeAnalysisResults(const CallGraphInfo& call_graph, InterruptHandlerResult& result);
    
    // ========================================================================
    // 单函数分析方法 (各个模块文件)
    // ========================================================================
    
    // 内存操作分析
    void analyzeMemoryOperationsInFunction(Function* function, InterruptHandlerResult& result);
    void analyzeStoreInstruction(StoreInst* store, MemoryWriteOperation& write_op, const std::string& func_name);
    void analyzeAtomicRMWInstruction(AtomicRMWInst* rmw, MemoryWriteOperation& write_op, const std::string& func_name);
    void analyzeAtomicCmpXchgInstruction(AtomicCmpXchgInst* cmpxchg, MemoryWriteOperation& write_op, const std::string& func_name);
    void consolidateWriteOperations(InterruptHandlerResult& result);
    bool isWriteOperation(const Instruction* inst);
    bool isReadOperation(const Instruction* inst);
    
    // 全局变量分析
    void analyzeGlobalWritesInFunction(Function* function, std::set<std::string>& modified_globals, 
                                     std::set<std::string>& modified_statics);
    void analyzeStoreGlobalAccess(StoreInst* store, std::set<std::string>& modified_globals,
                                 std::set<std::string>& modified_statics, const std::string& source_function);
    void analyzeGEPGlobalAccess(GetElementPtrInst* gep, std::set<std::string>& modified_globals,
                               std::set<std::string>& modified_statics, const std::string& source_function);
    void analyzeIndirectGlobalAccess(Value* ptr, std::set<std::string>& modified_globals,
                                    std::set<std::string>& modified_statics, const std::string& source_function);
    void analyzeAtomicGlobalAccess(AtomicRMWInst* atomic, std::set<std::string>& modified_globals,
                                  std::set<std::string>& modified_statics, const std::string& source_function);
    void analyzeAtomicCmpXchgGlobalAccess(AtomicCmpXchgInst* cmpxchg, std::set<std::string>& modified_globals,
                                         std::set<std::string>& modified_statics, const std::string& source_function);
    bool isGlobalOrStaticVariable(Value* value);
    std::string getVariableScope(GlobalVariable* gv);
    std::string analyzeGEPFieldAccess(GetElementPtrInst* gep);
    
    // 数据结构分析
    void analyzeDataStructuresInFunction(Function* function, std::map<std::string, DataStructureAccess>& unique_accesses);
    DataStructureAccess analyzeStructAccess(const GetElementPtrInst* gep);
    std::string analyzeAccessPattern(const GetElementPtrInst* gep, Type* field_type);
    
    // 类型分析辅助函数
    std::string getStructName(Type* type);
    std::string getFieldName(Type* struct_type, unsigned field_index);
    std::string getTypeName(Type* type);
    std::vector<std::string> getStructFields(StructType* struct_type);
    size_t getFieldOffset(StructType* struct_type, unsigned field_index);
    
    // ========================================================================
    // 函数指针分析 (FunctionPointerAnalyzer.cpp)
    // ========================================================================
    std::vector<std::string> resolveFunctionPointer(Value* func_ptr);
    std::vector<std::string> resolveWithHeuristics(Value* func_ptr);
    
#ifdef SVF_AVAILABLE
    // SVF辅助函数
    const SVF::Function* findSVFFunction(const std::string& name);
#endif
    
    // ========================================================================
    // 输出管理 (AnalysisOutputManager.cpp)
    // ========================================================================
    
    // 置信度计算
    double calculateConfidence(const InterruptHandlerResult& result);
    bool hasAdvancedAnalysisFeatures(const InterruptHandlerResult& result);
    
    // JSON输出生成
    json::Object createHandlerJson(const InterruptHandlerResult& result);
    void addBasicInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addMemoryOperationInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addDataStructureInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addFunctionCallInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addMemoryWriteInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addVariableModificationInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addFunctionPointerInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addFeatureFlags(json::Object& handler, const InterruptHandlerResult& result);
    json::Object createStatisticsJson(const std::vector<InterruptHandlerResult>& results);
    void writeJsonToFile(const json::Object& root, const std::string& output_file);
    
    // ========================================================================
    // 工具函数
    // ========================================================================
    std::string getInstructionLocation(const Instruction* inst);
    bool isDeviceRelatedFunction(const std::string& name);
    bool isInterruptRelatedFunction(const std::string& name);
    bool isWorkQueueFunction(const std::string& name);
    bool isInternalFunction(const std::string& name);
};

//===----------------------------------------------------------------------===//
// 内联函数实现
//===----------------------------------------------------------------------===//

inline SVFInterruptAnalyzer::SVFInterruptAnalyzer(LLVMContext* ctx) 
    : context(ctx), svf_initialized(false) {
}

inline SVFInterruptAnalyzer::~SVFInterruptAnalyzer() {
    cleanupSVFResources();
}

inline void SVFInterruptAnalyzer::cleanupSVFResources() {
#ifdef SVF_AVAILABLE
    // 按正确顺序清理SVF资源，避免段错误
    if (vfg) vfg.reset();
    if (pta) pta.reset(); 
    if (svfir) svfir.reset();
    modules.clear();
#endif
}

#endif // SVF_INTERRUPT_ANALYZER_H
            // ========================================================================
    // 内存操作分析 (MemoryAnalyzer.cpp)
    // ========================================================================
    void analyzeMemoryOperations(Function* handler, InterruptHandlerResult& result);
    bool isWriteOperation(const Instruction* inst);
    bool isReadOperation(const Instruction* inst);
    void consolidateWriteOperations(InterruptHandlerResult& result);
    void analyzeGlobalAndStaticWrites(Function* handler, InterruptHandlerResult& result);
    bool isGlobalOrStaticVariable(Value* value);
    std::string getVariableScope(GlobalVariable* gv);
    std::string analyzeGEPFieldAccess(GetElementPtrInst* gep);
    
    // 递归分析方法
    void analyzeMemoryOperationsRecursive(Function* function, InterruptHandlerResult& result,
                                         std::set<Function*>& analyzed_functions,
                                         std::set<std::string>& analyzed_function_pointers, int depth);
    void analyzeFunctionCallMemory(CallInst* call, InterruptHandlerResult& result,
                                  std::set<Function*>& analyzed_functions,
                                  std::set<std::string>& analyzed_function_pointers, int depth);
    MemoryWriteOperation analyzeWriteOperation(Instruction* inst, const std::string& source_function);
    std::string getValueName(Value* value);
    bool isInternalFunction(const std::string& name);
    //===- SVFInterruptAnalyzer.h - Enhanced SVF中断处理函数分析器 -----------===//
//
// 增强版SVF中断处理函数分析器
// 
// 核心功能:
// - 读写操作分离分析
// - 数据结构字段级跟踪
// - 函数指针精确解析
// - 全局/静态变量修改跟踪
// - 详细函数调用分析
//
//===----------------------------------------------------------------------===//

#ifndef SVF_INTERRUPT_ANALYZER_H
#define SVF_INTERRUPT_ANALYZER_H

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/JSON.h"

// SVF Headers - 条件编译
#ifdef SVF_AVAILABLE
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include "Graphs/VFG.h"
#include "Util/Options.h"
#endif

#include <memory>
#include <vector>
#include <string>
#include <map>
#include <set>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 数据结构定义
//===----------------------------------------------------------------------===//

/// 数据结构访问信息
struct DataStructureAccess {
    std::string struct_name;           // 结构体名称
    std::string field_name;            // 字段名称
    std::string access_pattern;        // 访问模式描述
    size_t offset;                     // 字段偏移
    std::string field_type;            // 字段类型
    bool is_pointer_field;             // 是否为指针字段
    
    DataStructureAccess() : offset(0), is_pointer_field(false) {}
};

/// 函数调用信息
struct FunctionCallInfo {
    std::string function_name;         // 函数名
    std::string call_type;             // 调用类型：direct, indirect, function_pointer
    size_t call_count;                 // 调用次数
    std::vector<std::string> call_sites; // 调用点信息
    std::vector<std::string> possible_targets; // 可能的目标（用于间接调用）
    
    FunctionCallInfo() : call_count(0) {}
};

/// 内存写操作信息
struct MemoryWriteOperation {
    std::string target_name;           // 写操作目标名称
    std::string target_type;           // 目标类型：global_var, static_var, struct_field
    std::string data_type;             // 数据类型
    size_t write_count;                // 写操作次数
    std::vector<std::string> write_locations; // 写操作位置
    bool is_critical;                  // 是否为关键写操作
    
    MemoryWriteOperation() : write_count(0), is_critical(false) {}
};

/// 分析结果数据结构
struct InterruptHandlerResult {
    // 基础信息
    std::string function_name;
    std::string source_file;
    std::string module_file;
    
    // 基础统计
    size_t total_instructions;
    size_t total_basic_blocks;
    size_t function_calls;
    size_t indirect_calls;
    size_t memory_read_operations;      // 分离读操作
    size_t memory_write_operations;     // 分离写操作
    
    // 增强的分析结果
    std::vector<DataStructureAccess> data_structure_accesses;  // 数据结构访问
    std::vector<FunctionCallInfo> function_call_details;       // 详细函数调用信息
    std::vector<MemoryWriteOperation> memory_writes;           // 内存写操作详情
    std::vector<std::string> modified_global_vars;             // 被修改的全局变量
    std::vector<std::string> modified_static_vars;             // 被修改的静态变量
    
    // SVF增强分析结果
    std::map<std::string, std::vector<std::string>> function_pointer_targets; // 函数指针目标
    std::vector<std::string> direct_function_calls;            // 直接函数调用
    std::vector<std::string> indirect_call_targets;            // 间接调用目标
    
    // 中断处理特征
    bool has_device_access;
    bool has_irq_operations;
    bool has_work_queue_ops;
    
    // 分析质量
    bool analysis_complete;
    double confidence_score;
    
    InterruptHandlerResult() : 
        total_instructions(0), total_basic_blocks(0), 
        function_calls(0), indirect_calls(0), 
        memory_read_operations(0), memory_write_operations(0),
        has_device_access(false), has_irq_operations(false), 
        has_work_queue_ops(false), analysis_complete(false), confidence_score(0.0) {}
};

//===----------------------------------------------------------------------===//
// 主分析器类
//===----------------------------------------------------------------------===//

class SVFInterruptAnalyzer {
private:
    // SVF核心组件
#ifdef SVF_AVAILABLE
    std::unique_ptr<SVF::SVFIR> svfir;
    std::unique_ptr<SVF::AndersenWaveDiff> pta;
    std::unique_ptr<SVF::VFG> vfg;
#endif
    
    // LLVM组件
    std::vector<std::unique_ptr<Module>> modules;
    LLVMContext* context;
    std::vector<std::string> loaded_bc_files;
    bool svf_initialized;
    
    // 分析缓存
    std::map<Type*, std::string> type_name_cache;
    std::map<std::string, std::vector<std::string>> struct_field_cache;

public:
    // 构造函数和析构函数
    explicit SVFInterruptAnalyzer(LLVMContext* ctx);
    ~SVFInterruptAnalyzer();
    
    // 禁用拷贝构造和赋值
    SVFInterruptAnalyzer(const SVFInterruptAnalyzer&) = delete;
    SVFInterruptAnalyzer& operator=(const SVFInterruptAnalyzer&) = delete;
    
    // 核心公共接口
    bool loadBitcodeFiles(const std::vector<std::string>& files);
    bool initializeSVF();
    std::vector<InterruptHandlerResult> analyzeInterruptHandlers(const std::vector<std::string>& handler_names);
    
    // 工具函数
    Function* findFunction(const std::string& name);
    size_t getModuleCount() const { return modules.size(); }
    
    // 输出和统计
    void outputResults(const std::vector<InterruptHandlerResult>& results, const std::string& output_file);
    void printStatistics() const;
    void printAnalysisBreakdown(const std::vector<InterruptHandlerResult>& results) const;

private:
    // ========================================================================
    // SVF核心功能 (SVFInterruptAnalyzer.cpp)
    // ========================================================================
    bool initializeSVFCore();
    bool runPointerAnalysis();
    bool buildVFG();
    void cleanupSVFResources();
    
    // ========================================================================
    // 分析流程控制 (SVFInterruptAnalyzer.cpp)
    // ========================================================================
    InterruptHandlerResult analyzeSingleHandler(Function* handler);
    void printAnalysisSummary(const InterruptHandlerResult& result);
    
    // ========================================================================
    // 内存操作分析 (MemoryAnalyzer.cpp)
    // ========================================================================
    void analyzeMemoryOperations(Function* handler, InterruptHandlerResult& result);
    bool isWriteOperation(const Instruction* inst);
    bool isReadOperation(const Instruction* inst);
    void consolidateWriteOperations(InterruptHandlerResult& result);
    void analyzeGlobalAndStaticWrites(Function* handler, InterruptHandlerResult& result);
    bool isGlobalOrStaticVariable(Value* value);
    std::string getVariableScope(GlobalVariable* gv);
    std::string analyzeGEPFieldAccess(GetElementPtrInst* gep);
    
    // ========================================================================
    // 数据结构分析 (DataStructureAnalyzer.cpp)
    // ========================================================================
    void analyzeDataStructures(Function* handler, InterruptHandlerResult& result);
    DataStructureAccess analyzeStructAccess(const GetElementPtrInst* gep);
    std::string analyzeAccessPattern(const GetElementPtrInst* gep, Type* field_type);
    void analyzeStructureUsagePatterns(Function* handler, InterruptHandlerResult& result);
    std::string getGEPStructName(GetElementPtrInst* gep);
    std::string analyzeInstructionUsage(Instruction* inst);
    
    // 类型分析辅助函数
    std::string getStructName(Type* type);
    std::string getFieldName(Type* struct_type, unsigned field_index);
    std::string getTypeName(Type* type);
    std::vector<std::string> getStructFields(StructType* struct_type);
    size_t getFieldOffset(StructType* struct_type, unsigned field_index);
    
    // ========================================================================
    // 函数调用分析 (FunctionCallAnalyzer.cpp)
    // ========================================================================
    void analyzeFunctionCalls(Function* handler, InterruptHandlerResult& result);
    void mergeCallInfo(std::map<std::string, FunctionCallInfo>& call_map, const FunctionCallInfo& call_info);
    void convertAndSortCallInfo(const std::map<std::string, FunctionCallInfo>& call_map, InterruptHandlerResult& result);
    
    // 函数特性分析
    void analyzeCalleeCharacteristics(Function* callee, FunctionCallInfo& call_info);
    void analyzeCallSignature(Function* callee, FunctionCallInfo& call_info);
    void analyzeIndirectCallPattern(CallInst* CI, FunctionCallInfo& call_info);
    
    // 调用模式分析
    void analyzeCallPatterns(InterruptHandlerResult& result);
    void analyzeCallChainPatterns(InterruptHandlerResult& result);
    void analyzeExceptionHandlingCalls(InterruptHandlerResult& result);
    void handleInvokeInstruction(InvokeInst* II, InterruptHandlerResult& result, std::map<std::string, FunctionCallInfo>& call_map);
    
    // 函数类型检测
    bool isDebugIntrinsic(const std::string& name);
    bool isMemoryRelatedFunction(const std::string& name);
    bool isLockingFunction(const std::string& name);
    bool isWorkQueueFunction(const std::string& name);
    bool isDeviceRelatedFunction(const std::string& name);
    bool isInterruptRelatedFunction(const std::string& name);
    
    // ========================================================================
    // 函数指针分析 (FunctionPointerAnalyzer.cpp)
    // ========================================================================
    std::map<std::string, std::vector<std::string>> analyzeFunctionPointers(Function* handler);
    std::vector<std::string> resolveFunctionPointer(Value* func_ptr);
    std::vector<std::string> resolveWithHeuristics(Value* func_ptr);
    
#ifdef SVF_AVAILABLE
    // SVF驱动的函数指针分析
    void analyzeFunctionPointerDeclarations(Function* handler, std::map<std::string, std::vector<std::string>>& pointer_targets);
    void analyzeFunctionPointerCalls(Function* handler, std::map<std::string, std::vector<std::string>>& pointer_targets);
    void analyzeStructFunctionPointers(Function* handler, std::map<std::string, std::vector<std::string>>& pointer_targets);
    
    // 函数指针使用分析
    std::string getFunctionPointerName(Instruction* inst);
    void analyzeFunctionPointerUsage(Instruction* ptr_inst, const std::string& ptr_name, std::map<std::string, std::vector<std::string>>& pointer_targets);
    void analyzeStoredFunctionPointer(StoreInst* store, const std::string& ptr_name, std::map<std::string, std::vector<std::string>>& pointer_targets);
    void analyzeCallContext(CallInst* call, const std::string& call_site, std::map<std::string, std::vector<std::string>>& pointer_targets);
    
    // 结构体函数指针分析
    bool isStructFunctionPointerAccess(GetElementPtrInst* gep);
    std::string getStructFunctionPointerName(GetElementPtrInst* gep);
    std::vector<std::string> analyzeStructFunctionPointerTargets(GetElementPtrInst* gep);
    std::vector<std::string> analyzeFunctionPointerLoad(LoadInst* load);
    std::string getGEPFieldInfo(GetElementPtrInst* gep);
    
    // 高级函数指针分析
    void analyzeCallbackRegistrations(Function* handler, std::map<std::string, std::vector<std::string>>& pointer_targets);
    void analyzeCallbackRegistrationCall(CallInst* call, const std::string& reg_func, std::map<std::string, std::vector<std::string>>& pointer_targets);
    void analyzeVTableAccess(Function* handler, std::map<std::string, std::vector<std::string>>& pointer_targets);
    bool isVTableAccess(GetElementPtrInst* gep);
    std::vector<std::string> analyzeVTableTargets(GetElementPtrInst* gep);
    
    // SVF辅助函数
    const SVF::Function* findSVFFunction(const std::string& name);
#endif
    
    // ========================================================================
    // 输出管理 (AnalysisOutputManager.cpp)
    // ========================================================================
    
    // 置信度计算
    double calculateConfidence(const InterruptHandlerResult& result);
    bool hasAdvancedAnalysisFeatures(const InterruptHandlerResult& result);
    
    // JSON输出生成
    json::Object createHandlerJson(const InterruptHandlerResult& result);
    void addBasicInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addMemoryOperationInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addDataStructureInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addFunctionCallInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addMemoryWriteInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addVariableModificationInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addFunctionPointerInfo(json::Object& handler, const InterruptHandlerResult& result);
    void addFeatureFlags(json::Object& handler, const InterruptHandlerResult& result);
    json::Object createStatisticsJson(const std::vector<InterruptHandlerResult>& results);
    void writeJsonToFile(const json::Object& root, const std::string& output_file);
    
    // ========================================================================
    // 工具函数
    // ========================================================================
    std::string getInstructionLocation(const Instruction* inst);
};

//===----------------------------------------------------------------------===//
// 内联函数实现
//===----------------------------------------------------------------------===//

inline SVFInterruptAnalyzer::SVFInterruptAnalyzer(LLVMContext* ctx) 
    : context(ctx), svf_initialized(false) {
}

inline SVFInterruptAnalyzer::~SVFInterruptAnalyzer() {
    cleanupSVFResources();
}

#endif // SVF_INTERRUPT_ANALYZER_H
