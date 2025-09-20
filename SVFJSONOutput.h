//===- SVFJSONOutput.h - SVF专用的JSON输出 -------------------------------===//

#ifndef SVF_JSON_OUTPUT_H
#define SVF_JSON_OUTPUT_H

#include "SVFAnalyzer.h"
#include "llvm/Support/JSON.h"
#include <string>
#include <vector>

using namespace llvm;

//===----------------------------------------------------------------------===//
// SVF专用JSON输出生成器
//===----------------------------------------------------------------------===//

class SVFJSONOutputGenerator {
public:
    /// 输出SVF分析结果
    void outputResults(const std::vector<SVFInterruptHandlerAnalysis>& results,
                      const std::string& output_file);
    
    /// 转换为JSON Value
    json::Value convertToJSON(const std::vector<SVFInterruptHandlerAnalysis>& results);

private:
    /// 转换单个处理函数分析
    json::Object convertHandlerAnalysis(const SVFInterruptHandlerAnalysis& analysis);
    
    /// 转换函数指针分析结果
    json::Object convertFunctionPointerResult(const SVFFunctionPointerResult& result);
    
    /// 转换结构体字段信息
    json::Object convertStructFieldInfo(const SVFStructFieldInfo& field);
    
    /// 转换内存访问模式
    json::Object convertAccessPattern(const SVFMemoryAccessPattern& pattern);
    
    /// 生成统计信息
    json::Object generateStatistics(const std::vector<SVFInterruptHandlerAnalysis>& results);
    
    /// 生成SVF特定的元数据
    json::Object generateSVFMetadata();
};

//===----------------------------------------------------------------------===//
// SVF报告生成器
//===----------------------------------------------------------------------===//

class SVFReportGenerator {
public:
    /// 生成Markdown报告
    void generateMarkdownReport(const std::vector<SVFInterruptHandlerAnalysis>& results,
                               const std::string& output_file);
    
    /// 生成函数指针分析摘要
    void generateFunctionPointerSummary(const std::vector<SVFInterruptHandlerAnalysis>& results,
                                       const std::string& output_file);
    
    /// 生成结构体使用报告
    void generateStructUsageReport(const std::vector<SVFInterruptHandlerAnalysis>& results,
                                  const std::string& output_file);

private:
    /// 生成处理函数摘要
    std::string generateHandlerSummary(const SVFInterruptHandlerAnalysis& analysis);
    
    /// 格式化函数指针目标列表
    std::string formatFunctionPointerTargets(const SVFFunctionPointerResult& result);
    
    /// 格式化结构体字段列表
    std::string formatStructFields(const std::vector<SVFStructFieldInfo>& fields);
};

#endif // SVF_JSON_OUTPUT_H
