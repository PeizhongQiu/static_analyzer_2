//===- SimpleEnhancedJSONOutput.h - 简化的增强JSON输出 -------------------===//
//
// 简化版本的增强JSON输出，去除复杂的可视化和详细报告功能
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_SIMPLE_ENHANCED_JSON_OUTPUT_H
#define IRQ_ANALYSIS_SIMPLE_ENHANCED_JSON_OUTPUT_H

#include "JSONOutput.h"
#include "EnhancedCrossModuleAnalyzer.h"
#include "SVFAnalyzer.h"
#include "llvm/Support/JSON.h"
#include <string>
#include <vector>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 简化的增强JSON输出生成器
//===----------------------------------------------------------------------===//

class EnhancedJSONOutputGenerator : public JSONOutputGenerator {
public:
    /// 输出增强分析结果（简化版）
    void outputEnhancedAnalysisResults(const std::vector<EnhancedInterruptHandlerAnalysis>& results,
                                     const std::string& output_file,
                                     bool include_svf_details = true);
    
    /// 转换增强处理函数分析（简化版）
    json::Object convertEnhancedHandlerAnalysis(const EnhancedInterruptHandlerAnalysis& analysis);
    
    /// 生成简化统计信息
    json::Object generateSimpleStatistics(const std::vector<EnhancedInterruptHandlerAnalysis>& results);
    
    /// 打印简化统计信息
    void printSimpleStatistics(const std::vector<EnhancedInterruptHandlerAnalysis>& results);
    
    /// 生成简化报告包
    void generateSimpleReports(const std::vector<EnhancedInterruptHandlerAnalysis>& results,
                              const std::string& output_dir);

private:
    /// 生成简化执行摘要
    void generateSimpleExecutiveSummary(const std::vector<EnhancedInterruptHandlerAnalysis>& results,
                                       const std::string& output_file);
    
    /// 生成简化安全报告
    void generateSimpleSecurityReport(const std::vector<EnhancedInterruptHandlerAnalysis>& results,
                                     const std::string& output_file);
    
    /// 生成简化fuzzing目标报告
    void generateSimpleFuzzingReport(const std::vector<EnhancedInterruptHandlerAnalysis>& results,
                                    const std::string& output_file);
};

//===----------------------------------------------------------------------===//
// 简化的分析结果比较器
//===----------------------------------------------------------------------===//

class SimpleAnalysisComparator {
public:
    /// 简单比较结果
    struct SimpleComparisonResult {
        size_t basic_handlers;
        size_t enhanced_handlers;
        double precision_improvement;
        size_t svf_enhancements;
        
        SimpleComparisonResult() : basic_handlers(0), enhanced_handlers(0), 
                                  precision_improvement(0.0), svf_enhancements(0) {}
    };
    
    /// 比较基础和增强分析结果
    static SimpleComparisonResult compareAnalyses(
        const std::vector<InterruptHandlerAnalysis>& basic_results,
        const std::vector<EnhancedInterruptHandlerAnalysis>& enhanced_results);
    
    /// 生成简单比较报告
    static void generateSimpleComparisonReport(const SimpleComparisonResult& result,
                                              const std::string& output_file);
};

#endif // IRQ_ANALYSIS_SIMPLE_ENHANCED_JSON_OUTPUT_H
