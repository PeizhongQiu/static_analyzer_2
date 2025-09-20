//===- SimpleAnalysisComparator.cpp - 简化的分析结果比较器 ---------------===//

#include "SimpleEnhancedJSONOutput.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <algorithm>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 简化的分析结果比较器实现
//===----------------------------------------------------------------------===//

SimpleAnalysisComparator::SimpleComparisonResult SimpleAnalysisComparator::compareAnalyses(
    const std::vector<InterruptHandlerAnalysis>& basic_results,
    const std::vector<EnhancedInterruptHandlerAnalysis>& enhanced_results) {
    
    SimpleComparisonResult result;
    result.basic_handlers = basic_results.size();
    result.enhanced_handlers = enhanced_results.size();
    
    if (basic_results.empty() || enhanced_results.empty()) {
        return result;
    }
    
    // 计算基础分析的平均置信度
    double basic_avg_confidence = 0.0;
    size_t basic_total_accesses = 0;
    
    for (const auto& analysis : basic_results) {
        for (const auto& access : analysis.total_memory_accesses) {
            basic_avg_confidence += access.confidence;
            basic_total_accesses++;
        }
    }
    
    if (basic_total_accesses > 0) {
        basic_avg_confidence /= basic_total_accesses;
    }
    
    // 计算增强分析的平均精度
    double enhanced_avg_precision = 0.0;
    size_t svf_enhancements = 0;
    
    for (const auto& analysis : enhanced_results) {
        enhanced_avg_precision += analysis.analysis_precision_score;
        
        // 统计SVF增强
        bool has_svf = false;
        for (const auto& access : analysis.enhanced_memory_accesses) {
            if (access.svf_enhanced) {
                has_svf = true;
                break;
            }
        }
        if (has_svf) {
            svf_enhancements++;
        }
    }
    
    enhanced_avg_precision /= enhanced_results.size();
    
    result.precision_improvement = enhanced_avg_precision - basic_avg_confidence;
    result.svf_enhancements = svf_enhancements;
    
    return result;
}

void SimpleAnalysisComparator::generateSimpleComparisonReport(
    const SimpleComparisonResult& result,
    const std::string& output_file) {
    
    std::ofstream file(output_file);
    if (!file.is_open()) {
        errs() << "Failed to create comparison report\n";
        return;
    }
    
    file << "# Analysis Comparison Report\n\n";
    
    file << "## Overview\n";
    file << "- Basic analysis handlers: " << result.basic_handlers << "\n";
    file << "- Enhanced analysis handlers: " << result.enhanced_handlers << "\n";
    file << "- SVF enhanced handlers: " << result.svf_enhancements << "\n\n";
    
    file << "## Improvements\n";
    if (result.precision_improvement > 0) {
        file << "✅ Precision improvement: +" << std::fixed << std::setprecision(1) 
             << result.precision_improvement << " points\n";
    } else {
        file << "⚠️ No significant precision improvement detected\n";
    }
    
    if (result.svf_enhancements > 0) {
        double enhancement_rate = (double)result.svf_enhancements / result.enhanced_handlers * 100.0;
        file << "✅ SVF enhancement rate: " << std::fixed << std::setprecision(1) 
             << enhancement_rate << "%\n";
    } else {
        file << "⚠️ No SVF enhancements detected\n";
    }
    
    file << "\n## Conclusion\n";
    if (result.precision_improvement > 5.0 && result.svf_enhancements > 0) {
        file << "🎯 Enhanced analysis provides significant improvements over basic analysis.\n";
    } else if (result.svf_enhancements > 0) {
        file << "📈 Enhanced analysis provides some improvements with SVF integration.\n";
    } else {
        file << "📊 Enhanced analysis completed, but limited improvements detected.\n";
        file << "Consider enabling SVF for better results.\n";
    }
    
    file.close();
    outs() << "Generated: comparison.md\n";
}
