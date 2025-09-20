//===- EnhancedCrossModuleAnalyzer.h - 带SVF的增强跨模块分析器 -------------===//
//
// 集成SVF功能的增强跨模块分析器
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_ENHANCED_CROSS_MODULE_ANALYZER_H
#define IRQ_ANALYSIS_ENHANCED_CROSS_MODULE_ANALYZER_H

#include "CrossModuleAnalyzer.h"
#include "SVFAnalyzer.h"
#include "llvm/IR/Module.h"
#include <memory>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 增强的分析结果数据结构
//===----------------------------------------------------------------------===//

struct EnhancedMemoryAccessInfo : public MemoryAccessInfo {
    // SVF增强的信息
    std::vector<std::string> svf_points_to_targets;
    std::string svf_analysis_method;
    bool svf_enhanced;
    int svf_precision_score;
    
    // 结构体字段详细信息
    std::vector<SVFStructFieldInfo> accessed_struct_fields;
    
    // 内存访问模式信息
    std::string access_pattern_name;
    bool is_part_of_pattern;
    
    EnhancedMemoryAccessInfo() : svf_enhanced(false), svf_precision_score(0), is_part_of_pattern(false) {}
    
    // 从基础MemoryAccessInfo构造
    EnhancedMemoryAccessInfo(const MemoryAccessInfo& base) 
        : MemoryAccessInfo(base), svf_enhanced(false), svf_precision_score(0), is_part_of_pattern(false) {}
};

struct EnhancedFunctionPointerTarget : public FunctionPointerTarget {
    // SVF增强的信息
    std::string svf_analysis_method;
    bool svf_verified;
    std::vector<std::string> call_graph_paths;  // 从调用点到目标的路径
    
    EnhancedFunctionPointerTarget(Function* f, int conf, const std::string& reason)
        : FunctionPointerTarget(f, conf, reason), svf_verified(false) {}
    
    EnhancedFunctionPointerTarget(const FunctionPointerTarget& base)
        : FunctionPointerTarget(base), svf_verified(false) {}
};

struct EnhancedInterruptHandlerAnalysis : public InterruptHandlerAnalysis {
    // SVF增强的分析结果
    std::vector<EnhancedMemoryAccessInfo> enhanced_memory_accesses;
    std::vector<EnhancedFunctionPointerTarget> enhanced_function_targets;
    std::map<std::string, std::vector<SVFStructFieldInfo>> struct_usage_analysis;
    std::vector<SVFMemoryAccessPattern> discovered_access_patterns;
    
    // 精度和质量指标
    double analysis_precision_score;
    std::string analysis_quality_level;  // "basic", "enhanced", "precise"
    
    EnhancedInterruptHandlerAnalysis() : analysis_precision_score(0.0), analysis_quality_level("basic") {}
    
    // 从基础分析构造
    EnhancedInterruptHandlerAnalysis(const InterruptHandlerAnalysis& base) 
        : InterruptHandlerAnalysis(base), analysis_precision_score(0.0), analysis_quality_level("basic") {}
};

//===----------------------------------------------------------------------===//
// 增强的跨模块分析器
//===----------------------------------------------------------------------===//

class EnhancedCrossModuleAnalyzer : public CrossModuleAnalyzer {
private:
    // SVF集成
    std::unique_ptr<SVFIntegrationHelper> svf_helper;
    bool svf_enabled;
    
    // 分析配置
    struct AnalysisConfig {
        bool enable_deep_struct_analysis;
        bool enable_pattern_discovery;
        bool enable_cross_module_dataflow;
        bool enable_precise_pointer_analysis;
        int max_analysis_depth;
        
        AnalysisConfig() : enable_deep_struct_analysis(true), enable_pattern_discovery(true),
                          enable_cross_module_dataflow(true), enable_precise_pointer_analysis(true),
                          max_analysis_depth(5) {}
    } config;
    
    // 缓存和统计
    std::map<Function*, std::vector<SVFMemoryAccessPattern>> pattern_cache;
    std::map<std::string, std::vector<SVFStructFieldInfo>> global_struct_analysis;
    size_t total_svf_enhancements;
    
    /// 初始化SVF集成
    bool initializeSVFIntegration();
    
    /// 使用SVF增强内存访问分析
    std::vector<EnhancedMemoryAccessInfo> enhanceMemoryAccessAnalysis(Function* F);
    
    /// 使用SVF增强函数指针分析
    std::vector<EnhancedFunctionPointerTarget> enhanceFunctionPointerAnalysis(Function* F);
    
    /// 深度结构体分析
    void performDeepStructAnalysis(Function* F, EnhancedInterruptHandlerAnalysis& analysis);
    
    /// 发现和分析内存访问模式
    void discoverMemoryAccessPatterns(Function* F, EnhancedInterruptHandlerAnalysis& analysis);
    
    /// 跨模块数据流分析
    void performCrossModuleDataFlowAnalysis(Function* F, EnhancedInterruptHandlerAnalysis& analysis);
    
    /// 计算分析精度分数
    double calculateAnalysisPrecisionScore(const EnhancedInterruptHandlerAnalysis& analysis);
    
    /// 确定分析质量等级
    std::string determineAnalysisQuality(const EnhancedInterruptHandlerAnalysis& analysis);
    
    /// 合并基础分析和SVF增强分析
    EnhancedInterruptHandlerAnalysis mergeAnalysisResults(const InterruptHandlerAnalysis& base,
                                                         const std::vector<EnhancedMemoryAccessInfo>& enhanced_mem,
                                                         const std::vector<EnhancedFunctionPointerTarget>& enhanced_fp);
    
public:
    EnhancedCrossModuleAnalyzer() : svf_enabled(false), total_svf_enhancements(0) {}
    
    ~EnhancedCrossModuleAnalyzer() = default;
    
    /// 设置分析配置
    void setDeepStructAnalysis(bool enable) { config.enable_deep_struct_analysis = enable; }
    void setPatternDiscovery(bool enable) { config.enable_pattern_discovery = enable; }
    void setCrossModuleDataFlow(bool enable) { config.enable_cross_module_dataflow = enable; }
    void setPrecisePointerAnalysis(bool enable) { config.enable_precise_pointer_analysis = enable; }
    void setMaxAnalysisDepth(int depth) { config.max_analysis_depth = depth; }
    
    /// 重写加载模块方法以初始化SVF
    bool loadAllModules(const std::vector<std::string>& bc_files, LLVMContext& Context) override;
    
    /// 增强的处理函数分析
    std::vector<EnhancedInterruptHandlerAnalysis> analyzeAllHandlersEnhanced(const std::string& handler_json);
    
    /// 增强的单个处理函数分析
    EnhancedInterruptHandlerAnalysis analyzeHandlerEnhanced(Function* F);
    
    /// 全局结构体使用情况分析
    std::map<std::string, std::vector<SVFStructFieldInfo>> analyzeGlobalStructUsage();
    
    /// 跨模块函数指针关系分析
    std::map<Function*, std::vector<EnhancedFunctionPointerTarget>> analyzeCrossModuleFunctionPointers();
    
    /// 内存访问模式总结
    std::vector<SVFMemoryAccessPattern> summarizeMemoryAccessPatterns();
    
    /// 获取SVF状态
    bool isSVFEnabled() const { return svf_enabled; }
    std::string getSVFStatus() const;
    
    /// 获取增强统计信息
    struct EnhancedStatistics {
        size_t total_handlers;
        size_t svf_enhanced_handlers;
        size_t precise_analyses;
        size_t discovered_patterns;
        size_t cross_module_dependencies;
        double average_precision_score;
        
        EnhancedStatistics() : total_handlers(0), svf_enhanced_handlers(0), precise_analyses(0),
                              discovered_patterns(0), cross_module_dependencies(0), average_precision_score(0.0) {}
    };
    
    EnhancedStatistics getEnhancedStatistics() const;
    void printEnhancedStatistics() const;
};

//===----------------------------------------------------------------------===//
// SVF增强的内存访问分析器
//===----------------------------------------------------------------------===//

class SVFEnhancedMemoryAnalyzer : public EnhancedCrossModuleMemoryAnalyzer {
private:
    SVFIntegrationHelper* svf_helper;
    
public:
    SVFEnhancedMemoryAnalyzer(CrossModuleAnalyzer* analyzer, 
                             DataFlowAnalyzer* dfa,
                             const DataLayout* DL,
                             SVFIntegrationHelper* svf)
        : EnhancedCrossModuleMemoryAnalyzer(analyzer, dfa, DL), svf_helper(svf) {}
    
    /// SVF增强的内存访问分析
    std::vector<EnhancedMemoryAccessInfo> analyzeWithSVFEnhancement(Function& F);
    
    /// 精确的指针别名分析
    bool mayAlias(Value* ptr1, Value* ptr2);
    
    /// 结构体字段级别的访问分析
    std::vector<SVFStructFieldInfo> analyzeStructFieldAccesses(Function& F);
};

//===----------------------------------------------------------------------===//
// SVF增强的函数指针分析器
//===----------------------------------------------------------------------===//

class SVFEnhancedFunctionPointerAnalyzer : public DeepFunctionPointerAnalyzer {
private:
    SVFIntegrationHelper* svf_helper;
    
public:
    SVFEnhancedFunctionPointerAnalyzer(EnhancedGlobalSymbolTable* symbols, 
                                      DataFlowAnalyzer* dfa,
                                      SVFIntegrationHelper* svf)
        : DeepFunctionPointerAnalyzer(symbols, dfa), svf_helper(svf) {}
    
    /// SVF增强的深度函数指针分析
    std::vector<EnhancedFunctionPointerTarget> analyzeDeepWithSVF(Value* fp_value);
    
    /// 精确的间接调用目标分析
    std::vector<Function*> getPreciseCallTargets(CallInst* CI);
    
    /// 函数指针数据流分析
    std::vector<Value*> traceFunctionPointerDataFlow(Value* fp_value);
};

#endif // IRQ_ANALYSIS_ENHANCED_CROSS_MODULE_ANALYZER_H
