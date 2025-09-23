//===- SVFInterruptAnalyzer.h - Enhanced SVF中断处理函数分析器 -----------===//
//
// 增强版SVF中断处理函数分析器
// 
// 核心功能:
// - 完整调用图构建 (函数和函数指针)
// - 基于调用图的全面内存操作分析
// - 递归数据结构分析
// - 全局/静态变量修改跟踪
// - 结构体信息增强 (真实字段名、偏移量等)
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

/// 结构体字段信息 - 增强版
struct StructFieldInfo {
    std::string field_name;            // 字段名称 (真实名称或field_N)
    std::string field_type;            // 字段类型
    size_t field_offset;               // 字段在结构体中的偏移
    size_t field_size;                 // 字段大小（字节）
    
    StructFieldInfo() : field_offset(0), field_size(0) {}
};

/// 数组元素信息
struct ArrayElementInfo {
    int index;                         // 数组索引（-1表示动态索引）
    size_t offset;                     // 元素偏移
    size_t element_size;               // 元素大小
    
    ArrayElementInfo() : index(-1), offset(0), element_size(0) {}
};

/// 增强的内存写操作信息
struct MemoryWriteOperation {
    std::string target_name;           // 写操作目标名称
    std::string target_type;           // 目标类型：global_var, static_var, struct_field, array_element
    std::string data_type;             // 数据类型
    size_t write_count;                // 写操作次数
    std::vector<std::string> write_locations; // 写操作位置
    bool is_critical;                  // 是否为关键写操作
    
    // 新增：结构体相关信息（仅当target_type为struct_field时有效）
    std::string struct_name;           // 结构体名称（清理后的，如"test_device"）
    std::string field_name;            // 字段名称（真实名称或field_N）
    std::string field_type;            // 字段类型
    size_t field_offset;               // 字段偏移
    size_t field_size;                 // 字段大小
    std::string full_path;             // 完整路径（如"dev.test_device::regs"）
    
    MemoryWriteOperation() : write_count(0), is_critical(false), 
                           field_offset(0), field_size(0) {}
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
#ifdef SVF_AVAILABLE
    bool initializeSVFCore();
    bool runPointerAnalysis();
    bool buildVFG();
    const SVF::Function* findSVFFunction(const std::string& name);
#endif
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
    // 内存操作分析 (MemoryAnalyzer.cpp)
    // ========================================================================
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
    
    // ========================================================================
    // 数据结构分析 (DataStructureAnalyzer.cpp 或 EnhancedDataStructureAnalyzer.cpp)
    // ========================================================================
    void analyzeDataStructuresInFunction(Function* function, std::map<std::string, DataStructureAccess>& unique_accesses);
    DataStructureAccess analyzeStructAccess(const GetElementPtrInst* gep);
    std::string analyzeAccessPattern(const GetElementPtrInst* gep, Type* field_type);
    
    // 增强的GEP分析方法 (如果使用EnhancedDataStructureAnalyzer.cpp)
    void analyzeGEPWriteOperation(GetElementPtrInst* gep, MemoryWriteOperation& write_op, const std::string& func_name);
    StructFieldInfo analyzeGEPFieldInfo(GetElementPtrInst* gep, StructType* struct_type);
    ArrayElementInfo analyzeGEPArrayInfo(GetElementPtrInst* gep, Type* array_type);
    
    // 类型分析辅助函数
    std::string getStructName(Type* type);
    std::string getFieldName(Type* struct_type, unsigned field_index);
    std::string getTypeName(Type* type);
    std::vector<std::string> getStructFields(StructType* struct_type);
    size_t getFieldOffset(StructType* struct_type, unsigned field_index);
    
    // 增强的类型和字段分析辅助方法
    size_t calculateFieldOffset(StructType* struct_type, unsigned field_index);
    size_t calculateTypeSize(Type* type);
    std::string extractRealFieldName(StructType* struct_type, unsigned field_index);
    
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
    
    // ========================================================================
    // 函数指针分析 (FunctionPointerAnalyzer.cpp)
    // ========================================================================
    std::vector<std::string> resolveFunctionPointer(Value* func_ptr);
    std::vector<std::string> resolveWithHeuristics(Value* func_ptr);
    
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
    // 工具函数和类型检测
    // ========================================================================
    std::string getInstructionLocation(const Instruction* inst);
    bool isDeviceRelatedFunction(const std::string& name);
    bool isInterruptRelatedFunction(const std::string& name);
    bool isWorkQueueFunction(const std::string& name);
    bool isInternalFunction(const std::string& name);
    
    // 函数类型检测辅助方法
    bool isDebugIntrinsic(const std::string& name);
    bool isMemoryRelatedFunction(const std::string& name);
    bool isLockingFunction(const std::string& name);
};

//===----------------------------------------------------------------------===//
// 内联函数实现
//===----------------------------------------------------------------------===//

inline SVFInterruptAnalyzer::SVFInterruptAnalyzer(LLVMContext* ctx) 
    : context(ctx), svf_initialized(false) {
}

inline SVFInterruptAnalyzer::~SVFInterruptAnalyzer() {
    // cleanupSVFResources();
}

inline void SVFInterruptAnalyzer::cleanupSVFResources() {
#ifdef SVF_AVAILABLE
    // 按正确顺序清理SVF资源，避免段错误
    if (vfg) vfg.reset();
    if (pta) pta.reset(); 
    if (svfir) svfir.reset();
#endif
    modules.clear();
}

#endif // SVF_INTERRUPT_ANALYZER_H