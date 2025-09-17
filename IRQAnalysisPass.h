//===- IRQAnalysisPass.h - Main IRQ Analysis Pass -----------------------===//
//
// 主分析Pass，协调各个分析器
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_PASS_H
#define IRQ_ANALYSIS_PASS_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "DataStructures.h"
#include "IRQHandlerIdentifier.h"
#include "MemoryAccessAnalyzer.h"
#include "FunctionPointerAnalyzer.h"
#include "FunctionCallAnalyzer.h"
#include "InlineAsmAnalyzer.h"
#include "JSONOutput.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// 主分析Pass
//===----------------------------------------------------------------------===//

class IRQAnalysisPass : public ModulePass {
private:
    std::string output_path;
    std::string handler_json_path;
    
    /// 分析单个中断处理函数
    InterruptHandlerAnalysis analyzeSingleHandler(Function *F,
                                                 MemoryAccessAnalyzer &mem_analyzer,
                                                 FunctionCallAnalyzer &call_analyzer,
                                                 FunctionPointerAnalyzer &fp_analyzer,
                                                 InlineAsmAnalyzer &asm_analyzer);
    
    /// 检测递归调用
    bool detectRecursiveCalls(Function &F);
    
    /// 递归调用检测的辅助函数
    bool detectRecursiveCallsHelper(Function *F, 
                                   std::set<Function*> &visited,
                                   std::set<Function*> &in_path);
    
public:
    static char ID;
    
    IRQAnalysisPass(const std::string& output = "irq_analysis_results.json",
                   const std::string& handler_json = "") 
        : ModulePass(ID), output_path(output), handler_json_path(handler_json) {}
    
    bool runOnModule(Module &M) override;
    
    void getAnalysisUsage(AnalysisUsage &AU) const override;
    
    /// 设置输出路径
    void setOutputPath(const std::string& path) { output_path = path; }
    
    /// 设置handler.json路径
    void setHandlerJsonPath(const std::string& path) { handler_json_path = path; }
    
    /// 获取输出路径
    const std::string& getOutputPath() const { return output_path; }
    
    /// 获取handler.json路径
    const std::string& getHandlerJsonPath() const { return handler_json_path; }
};

#endif // IRQ_ANALYSIS_PASS_H
