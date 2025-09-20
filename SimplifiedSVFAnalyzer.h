//===- SimplifiedSVFAnalyzer.h - 简化的SVF分析器 -------------------------===//

#ifndef SIMPLIFIED_SVF_ANALYZER_H
#define SIMPLIFIED_SVF_ANALYZER_H

#include "SVFDataStructures.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"

// SVF Headers
#ifdef SVF_AVAILABLE
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include "WPA/FlowSensitive.h"
#include "Graphs/VFG.h"
#include "Util/Options.h"
#endif

#include <memory>
#include <vector>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 简化的SVF分析器 - 专注核心功能
//===----------------------------------------------------------------------===//

class SimplifiedSVFAnalyzer {
private:
#ifdef SVF_AVAILABLE
    std::unique_ptr<SVF::SVFIR> svfir;
    std::unique_ptr<SVF::AndersenWaveDiff> ander_pta;
    std::unique_ptr<SVF::FlowSensitive> flow_pta;
    std::unique_ptr<SVF::VFG> vfg;
#endif
    
    // 配置选项
    bool enable_flow_sensitive;
    bool enable_field_sensitive;
    int max_analysis_time;
    
    // 缓存
    std::map<CallInst*, SVFFunctionPointerResult> fp_cache;
    std::map<Value*, SVFPointerAnalysisResult> ptr_cache;
    
    /// 初始化SVF
    bool initializeSVF(const std::vector<std::unique_ptr<Module>>& modules);
    
    /// 运行指针分析
    bool runPointerAnalysis();
    
public:
    SimplifiedSVFAnalyzer() : enable_flow_sensitive(true), enable_field_sensitive(true),
                             max_analysis_time(600) {}
    
    /// 检查SVF可用性
    static bool isSVFAvailable();
    
    /// 获取SVF版本
    static std::string getSVFVersion();
    
    /// 设置分析选项
    void setFlowSensitive(bool enable) { enable_flow_sensitive = enable; }
    void setFieldSensitive(bool enable) { enable_field_sensitive = enable; }
    void setMaxAnalysisTime(int seconds) { max_analysis_time = seconds; }
    
    /// 初始化分析器
    bool initialize(const std::vector<std::unique_ptr<Module>>& modules);
    
    /// 分析中断处理函数
    SVFInterruptHandlerAnalysis analyzeHandler(Function* F);
    
    /// 分析函数指针
    std::vector<SVFFunctionPointerResult> analyzeFunctionPointers(Function* F);
    
    /// 分析指针
    std::vector<SVFPointerAnalysisResult> analyzePointers(Function* F);
    
    /// 分析结构体使用
    std::map<std::string, std::vector<SVFStructFieldInfo>> analyzeStructUsage(Function* F);
    
    /// 发现访问模式
    std::vector<SVFMemoryAccessPattern> discoverAccessPatterns(Function* F);
    
    /// 打印统计信息
    void printStatistics() const;
    
    /// 清理缓存
    void clearCache();
};

#endif // SIMPLIFIED_SVF_ANALYZER_H
