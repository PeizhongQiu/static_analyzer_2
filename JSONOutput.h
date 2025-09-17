//===- JSONOutput.h - JSON Output Utility -------------------------------===//
//
// 将分析结果输出为JSON格式的工具类
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_JSON_OUTPUT_H
#define IRQ_ANALYSIS_JSON_OUTPUT_H

#include "DataStructures.h"
#include "llvm/Support/JSON.h"
#include <string>
#include <vector>

using namespace llvm;

//===----------------------------------------------------------------------===//
// JSON输出工具
//===----------------------------------------------------------------------===//

class JSONOutputGenerator {
private:
    /// 转换内存访问信息为JSON对象
    json::Object convertMemoryAccess(const MemoryAccessInfo& access);
    
    /// 转换寄存器访问信息为JSON对象
    json::Object convertRegisterAccess(const RegisterAccessInfo& reg_access);
    
    /// 转换函数调用信息为JSON对象
    json::Object convertFunctionCall(const FunctionCallInfo& call);
    
    /// 转换指针链元素为JSON对象
    json::Object convertPointerChainElement(const PointerChainElement& elem);
    
    /// 转换间接调用分析为JSON对象
    json::Object convertIndirectCallAnalysis(const IndirectCallAnalysis& indirect);
    
    /// 转换函数指针目标为JSON对象
    json::Object convertFunctionPointerTarget(const FunctionPointerTarget& target);
    
    /// 转换中断处理函数分析为JSON对象
    json::Object convertHandlerAnalysis(const InterruptHandlerAnalysis& analysis);
    
public:
    /// 生成完整的JSON输出
    void outputAnalysisResults(const std::vector<InterruptHandlerAnalysis>& results,
                              const std::string& output_file);
    
    /// 转换为JSON Value（用于其他用途）
    json::Value convertToJSON(const std::vector<InterruptHandlerAnalysis>& results);
};

#endif // IRQ_ANALYSIS_JSON_OUTPUT_H
