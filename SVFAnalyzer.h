//===- SVFAnalyzer.h - SVF Integration for Advanced Analysis -------------===//
//
// SVF (Static Value-Flow Analysis) 集成用于高级函数指针和结构体分析
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_SVF_ANALYZER_H
#define IRQ_ANALYSIS_SVF_ANALYZER_H

#include "DataStructures.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

// SVF Headers - 检测是否安装了SVF
#ifdef SVF_AVAILABLE
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include "WPA/FlowSensitive.h"
#include "Graphs/VFG.h"
#include "SABER/LeakChecker.h"
#include "MemoryModel/PointerAnalysis.h"
#include "Util/Options.h"
#endif

#include <memory>
#include <map>
#include <set>
#include <vector>

using namespace llvm;

//===----------------------------------------------------------------------===//
// SVF分析结果数据结构
//===----------------------------------------------------------------------===//

struct SVFFunctionPointerResult {
    Function* source_function;
    CallInst* call_site;
    std::vector<Function*> possible_targets;
    std::map<Function*, int> confidence_scores;
    std::string analysis_method;  // "andersen", "flow_sensitive", "field_sensitive"
    bool is_precise;
    
    SVFFunctionPointerResult() : source_function(nullptr), call_site(nullptr), is_precise(false) {}
};

struct SVFStructFieldInfo {
    std::string struct_name;
    std::string field_name;
    unsigned field_index;
    Type* field_type;
    size_t offset_bytes;
    bool is_function_pointer;
    std::vector<Function*> stored_functions;  // 如果是函数指针字段
    
    SVFStructFieldInfo() : field_index(0), field_type(nullptr), 
                          offset_bytes(0), is_function_pointer(false) {}
};

struct SVFPointerAnalysisResult {
    Value* pointer;
    std::string pointer_description;
    std::set<Value*> points_to_set;
    std::vector<SVFStructFieldInfo> accessed_fields;
    bool is_global_pointer;
    bool is_heap_pointer;
    bool is_stack_pointer;
    int precision_score;  // 0-100
    
    SVFPointerAnalysisResult() : pointer(nullptr), is_global_pointer(false),
                                is_heap_pointer(false), is_stack_pointer(false),
                                precision_score(0) {}
};

struct SVFMemoryAccessPattern {
    std::string pattern_name;
    std::vector<Value*> access_sequence;
    std::string struct_type_chain;
    bool is_device_access_pattern;
    bool is_kernel_data_structure;
    int frequency;
    
    SVFMemoryAccessPattern() : is_device_access_pattern(false), 
                              is_kernel_data_structure(false), frequency(0) {}
};

//===----------------------------------------------------------------------===//
// SVF 分析器主类
//===----------------------------------------------------------------------===//

class SVFAnalyzer {
private:
#ifdef SVF_AVAILABLE
    std::unique_ptr<SVF::SVFIR> svfir;
    std::unique_ptr<SVF::AndersenWaveDiff> ander_pta;
    std::unique_ptr<SVF::FlowSensitive> flow_pta;
    std::unique_ptr<SVF::VFG> vfg;
    std::unique_ptr<SVF::SVFModule> svf_module;
#endif
    
    // 分析缓存
    std::map<CallInst*, SVFFunctionPointerResult> fp_analysis_cache;
    std::map<Value*, SVFPointerAnalysisResult> pointer_analysis_cache;
    std::map<StructType*, std::vector<SVFStructFieldInfo>> struct_info_cache;
    std::vector<SVFMemoryAccessPattern> discovered_patterns;
    
    // 配置选项
    bool enable_flow_sensitive;
    bool enable_field_sensitive;
    bool enable_context_sensitive;
    int max_analysis_time;  // 秒
    
    /// 初始化SVF基础设施
    bool initializeSVF(const std::vector<std::unique_ptr<Module>>& modules);
    
    /// 运行指针分析
    bool runPointerAnalysis();
    
    /// 分析函数指针调用点
    SVFFunctionPointerResult analyzeFunctionPointerCall(CallInst* CI);
    
    /// 分析结构体类型信息
    std::vector<SVFStructFieldInfo> analyzeStructType(StructType* ST);
    
    /// 分析指针的points-to集合
    SVFPointerAnalysisResult analyzePointer(Value* ptr);
    
    /// 发现内存访问模式
    void discoverMemoryAccessPatterns(Function* F);
    
    /// 分析设备相关的访问模式
    bool isDeviceAccessPattern(const std::vector<Value*>& access_seq);
    
    /// 分析内核数据结构访问
    bool isKernelDataStructureAccess(Value* ptr);
    
    /// 计算分析精度分数
    int calculatePrecisionScore(const std::set<Value*>& points_to_set);
    
#ifdef SVF_AVAILABLE
    /// SVF节点到LLVM Value的映射
    Value* svfNodeToLLVMValue(const SVF::PAGNode* node);
    
    /// 获取函数指针的可能目标
    std::vector<Function*> getFunctionTargets(const SVF::CallInst* cs);
    
    /// 分析结构体字段访问
    void analyzeStructFieldAccess(const SVF::GepObjPN* gepNode, SVFStructFieldInfo& fieldInfo);
#endif
    
public:
    SVFAnalyzer() : enable_flow_sensitive(true), enable_field_sensitive(true),
                   enable_context_sensitive(false), max_analysis_time(300) {}
    
    ~SVFAnalyzer() = default;
    
    /// 设置分析选项
    void setFlowSensitive(bool enable) { enable_flow_sensitive = enable; }
    void setFieldSensitive(bool enable) { enable_field_sensitive = enable; }
    void setContextSensitive(bool enable) { enable_context_sensitive = enable; }
    void setMaxAnalysisTime(int seconds) { max_analysis_time = seconds; }
    
    /// 初始化SVF分析器
    bool initialize(const std::vector<std::unique_ptr<Module>>& modules);
    
    /// 检查SVF是否可用
    static bool isSVFAvailable();
    
    /// 获取SVF版本信息
    static std::string getSVFVersion();
    
    /// 分析单个函数中的函数指针调用
    std::vector<SVFFunctionPointerResult> analyzeFunctionPointers(Function* F);
    
    /// 分析指针和内存访问
    std::vector<SVFPointerAnalysisResult> analyzePointers(Function* F);
    
    /// 分析结构体使用情况
    std::map<std::string, std::vector<SVFStructFieldInfo>> analyzeStructUsage(Function* F);
    
    /// 发现内存访问模式
    std::vector<SVFMemoryAccessPattern> discoverAccessPatterns(Function* F);
    
    /// 增强现有的内存访问分析
    MemoryAccessInfo enhanceMemoryAccessInfo(const MemoryAccessInfo& original, Value* ptr);
    
    /// 增强函数指针分析
    std::vector<FunctionPointerTarget> enhanceFunctionPointerAnalysis(CallInst* CI);
    
    /// 获取全局统计信息
    void printStatistics() const;
    
    /// 清理缓存
    void clearCache();
};

//===----------------------------------------------------------------------===//
// SVF集成助手类
//===----------------------------------------------------------------------===//

class SVFIntegrationHelper {
private:
    SVFAnalyzer* svf_analyzer;
    bool svf_available;
    
public:
    SVFIntegrationHelper() : svf_analyzer(nullptr), svf_available(false) {}
    
    ~SVFIntegrationHelper() {
        delete svf_analyzer;
    }
    
    /// 初始化SVF集成
    bool initialize(const std::vector<std::unique_ptr<Module>>& modules);
    
    /// 检查SVF是否已初始化
    bool isInitialized() const { return svf_available && svf_analyzer; }
    
    /// 获取SVF分析器实例
    SVFAnalyzer* getAnalyzer() const { return svf_analyzer; }
    
    /// 创建增强的内存访问信息
    MemoryAccessInfo createEnhancedMemoryAccess(const MemoryAccessInfo& original, Value* ptr);
    
    /// 创建增强的函数指针分析
    std::vector<FunctionPointerTarget> createEnhancedFunctionPointerTargets(CallInst* CI);
    
    /// 打印SVF状态信息
    void printStatus() const;
};

#endif // IRQ_ANALYSIS_SVF_ANALYZER_H
