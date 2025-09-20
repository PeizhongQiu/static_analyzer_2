//===- SVFAnalyzer.h - 专注SVF的中断处理分析器 ---------------------------===//

#ifndef SVF_IRQ_ANALYZER_H
#define SVF_IRQ_ANALYZER_H

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

// SVF Headers
#ifdef SVF_AVAILABLE
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include "WPA/FlowSensitive.h"
#include "Graphs/VFG.h"
#include "MemoryModel/PointerAnalysis.h"
#include "Util/Options.h"
#endif

#include <memory>
#include <map>
#include <set>
#include <vector>
#include <string>

using namespace llvm;

//===----------------------------------------------------------------------===//
// SVF分析结果数据结构
//===----------------------------------------------------------------------===//

struct SVFFunctionPointerResult {
    Function* source_function;
    CallInst* call_site;
    std::vector<Function*> possible_targets;
    std::map<Function*, int> confidence_scores;
    std::string analysis_method;
    bool is_precise;
    
    SVFFunctionPointerResult() : source_function(nullptr), call_site(nullptr), is_precise(false) {}
};

struct SVFStructFieldInfo {
    std::string struct_name;
    std::string field_name;
    unsigned field_index;
    Type* field_type;
    bool is_function_pointer;
    std::vector<Function*> stored_functions;
    
    SVFStructFieldInfo() : field_index(0), field_type(nullptr), is_function_pointer(false) {}
};

struct SVFMemoryAccessPattern {
    std::string pattern_name;
    std::vector<Value*> access_sequence;
    bool is_device_access_pattern;
    bool is_kernel_data_structure;
    int frequency;
    
    SVFMemoryAccessPattern() : is_device_access_pattern(false), is_kernel_data_structure(false), frequency(0) {}
};

struct SVFInterruptHandlerAnalysis {
    std::string function_name;
    std::string source_file;
    
    // SVF特定分析结果
    std::vector<SVFFunctionPointerResult> function_pointer_calls;
    std::map<std::string, std::vector<SVFStructFieldInfo>> struct_usage;
    std::vector<SVFMemoryAccessPattern> access_patterns;
    std::set<Value*> pointed_objects;
    
    // 分析质量指标
    double svf_precision_score;
    bool svf_analysis_complete;
    
    SVFInterruptHandlerAnalysis() : svf_precision_score(0.0), svf_analysis_complete(false) {}
};

//===----------------------------------------------------------------------===//
// SVF核心分析器
//===----------------------------------------------------------------------===//

class SVFAnalyzer {
private:
#ifdef SVF_AVAILABLE
    std::unique_ptr<SVF::SVFIR> svfir;
    std::unique_ptr<SVF::AndersenWaveDiff> ander_pta;
    std::unique_ptr<SVF::FlowSensitive> flow_pta;
    std::unique_ptr<SVF::VFG> vfg;
#endif
    
    // 分析缓存
    std::map<CallInst*, SVFFunctionPointerResult> fp_cache;
    std::map<StructType*, std::vector<SVFStructFieldInfo>> struct_cache;
    
    // 配置
    bool enable_flow_sensitive;
    bool enable_field_sensitive;
    int max_analysis_time;
    
public:
    SVFAnalyzer() : enable_flow_sensitive(true), enable_field_sensitive(true), max_analysis_time(300) {}
    
    /// 检查SVF是否可用
    static bool isSVFAvailable();
    
    /// 初始化SVF分析器
    bool initialize(const std::vector<std::unique_ptr<Module>>& modules);
    
    /// 分析单个中断处理函数
    SVFInterruptHandlerAnalysis analyzeHandler(Function* handler);
    
    /// 分析函数指针调用
    SVFFunctionPointerResult analyzeFunctionPointer(CallInst* call);
    
    /// 分析结构体使用情况
    std::map<std::string, std::vector<SVFStructFieldInfo>> analyzeStructUsage(Function* F);
    
    /// 发现内存访问模式
    std::vector<SVFMemoryAccessPattern> discoverAccessPatterns(Function* F);
    
    /// 获取指针指向的对象集合
    std::set<Value*> getPointsToSet(Value* pointer);
    
    /// 计算分析精度分数
    double calculatePrecisionScore(const SVFInterruptHandlerAnalysis& analysis);
    
    /// 打印统计信息
    void printStatistics() const;
    
    /// 清理缓存
    void clearCache();

private:
    /// 初始化SVF基础设施
    bool initializeSVF(const std::vector<std::unique_ptr<Module>>& modules);
    
    /// 运行指针分析
    bool runPointerAnalysis();
    
    /// 分析函数指针目标
    std::vector<Function*> getFunctionTargets(CallInst* call);
    
    /// 分析结构体字段
    std::vector<SVFStructFieldInfo> analyzeStructType(StructType* ST);
    
    /// 检查是否是设备访问模式
    bool isDeviceAccessPattern(const std::vector<Value*>& access_seq);
    
    /// 检查是否是内核数据结构访问
    bool isKernelDataStructureAccess(Value* ptr);

#ifdef SVF_AVAILABLE
    /// SVF节点到LLVM Value的转换
    Value* svfNodeToLLVMValue(const SVF::PAGNode* node);
#endif
};

//===----------------------------------------------------------------------===//
// SVF中断处理分析器主类
//===----------------------------------------------------------------------===//

class SVFIRQAnalyzer {
private:
    std::unique_ptr<SVFAnalyzer> svf_analyzer;
    std::vector<std::unique_ptr<Module>> modules;
    LLVMContext* context;
    
public:
    SVFIRQAnalyzer(LLVMContext* ctx) : context(ctx) {}
    
    /// 加载bitcode模块
    bool loadModules(const std::vector<std::string>& bc_files);
    
    /// 分析所有中断处理函数
    std::vector<SVFInterruptHandlerAnalysis> analyzeAllHandlers(const std::vector<std::string>& handler_names);
    
    /// 获取SVF分析器
    SVFAnalyzer* getSVFAnalyzer() { return svf_analyzer.get(); }
    
    /// 检查是否成功初始化
    bool isInitialized() const { return svf_analyzer && svf_analyzer->isSVFAvailable(); }
};

#endif // SVF_IRQ_ANALYZER_H
