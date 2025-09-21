//===- SVFInterruptAnalyzer.h - SVF中断处理函数分析器 -------------------===//

#ifndef SVF_INTERRUPT_ANALYZER_H
#define SVF_INTERRUPT_ANALYZER_H

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"

// SVF Headers
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
// 分析结果数据结构
//===----------------------------------------------------------------------===//

struct InterruptHandlerResult {
    std::string function_name;
    std::string source_file;
    std::string module_file;
    
    // 基础统计
    size_t total_instructions;
    size_t total_basic_blocks;
    size_t function_calls;
    size_t indirect_calls;
    size_t memory_operations;
    
    // SVF分析结果
    std::vector<std::string> indirect_call_targets;
    std::map<std::string, std::vector<std::string>> pointer_analysis;
    std::vector<std::string> accessed_global_variables;
    std::vector<std::string> called_functions;
    
    // 中断处理特征
    bool has_device_access;
    bool has_irq_operations;
    bool has_work_queue_ops;
    
    // 分析质量
    bool analysis_complete;
    double confidence_score;
    
    InterruptHandlerResult() : total_instructions(0), total_basic_blocks(0), 
                              function_calls(0), indirect_calls(0), memory_operations(0),
                              has_device_access(false), has_irq_operations(false), 
                              has_work_queue_ops(false), analysis_complete(false), confidence_score(0.0) {}
};

//===----------------------------------------------------------------------===//
// SVF中断处理分析器
//===----------------------------------------------------------------------===//

class SVFInterruptAnalyzer {
private:
#ifdef SVF_AVAILABLE
    std::unique_ptr<SVF::SVFIR> svfir;
    std::unique_ptr<SVF::AndersenWaveDiff> pta;
    std::unique_ptr<SVF::VFG> vfg;
#endif
    
    std::vector<std::unique_ptr<Module>> modules;
    LLVMContext* context;
    std::vector<std::string> loaded_bc_files;
    bool svf_initialized;
    
public:
    SVFInterruptAnalyzer(LLVMContext* ctx) : context(ctx), svf_initialized(false) {}
    ~SVFInterruptAnalyzer() {
        // 按正确顺序清理SVF资源，避免段错误
        #ifdef SVF_AVAILABLE
        // 1. 首先清理VFG（依赖于其他组件）
        if (vfg) {
            vfg.reset();
        }
        
        // 2. 然后清理指针分析（依赖于SVFIR）
        if (pta) {
            pta.reset();
        }
        
        // 3. 最后清理SVFIR
        if (svfir) {
            svfir.reset();
        }
        
        // 4. 清理模块（让LLVM自己处理）
        modules.clear();
        #endif
    }
    
    /// 加载bitcode文件
    bool loadBitcodeFiles(const std::vector<std::string>& files);
    
    /// 初始化SVF分析
    bool initializeSVF();
    
    /// 分析指定的中断处理函数
    std::vector<InterruptHandlerResult> analyzeInterruptHandlers(const std::vector<std::string>& handler_names);
    
    /// 查找函数
    Function* findFunction(const std::string& name);
    
    /// 输出结果到JSON
    void outputResults(const std::vector<InterruptHandlerResult>& results, const std::string& output_file);
    
    /// 打印统计信息
    void printStatistics() const;
    
    /// 获取加载的模块数量
    size_t getModuleCount() const { return modules.size(); }

private:
    /// SVF初始化核心逻辑
    bool initializeSVFCore();
    
    /// 运行指针分析
    bool runPointerAnalysis();
    
    /// 构建VFG
    bool buildVFG();
    
    /// 分析单个函数
    InterruptHandlerResult analyzeSingleHandler(Function* handler);
    
    /// SVF函数指针分析
    std::vector<std::string> analyzeIndirectCalls(Function* handler);
    
    /// SVF指针分析
    std::map<std::string, std::vector<std::string>> analyzePointers(Function* handler);
    
    /// 检测中断处理特征
    void detectInterruptFeatures(Function* handler, InterruptHandlerResult& result);
    
    /// 计算置信度分数
    double calculateConfidence(const InterruptHandlerResult& result);
    
    /// 查找SVF函数
    const SVF::Function* findSVFFunction(const std::string& name);
    
    /// 辅助函数
    bool isInterruptRelatedFunction(const std::string& name);
    bool isDeviceRelatedFunction(const std::string& name);
    std::string getInstructionInfo(const Instruction* inst);
};

#endif // SVF_INTERRUPT_ANALYZER_H
