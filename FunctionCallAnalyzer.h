//===- FunctionCallAnalyzer.h - Function Call Analyzer ------------------===//
//
// 分析函数调用，包括直接调用和间接调用
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_FUNCTION_CALL_ANALYZER_H
#define IRQ_ANALYSIS_FUNCTION_CALL_ANALYZER_H

#include "DataStructures.h"
#include "FunctionPointerAnalyzer.h"
#include "llvm/IR/Function.h"
#include <set>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 函数调用分析器
//===----------------------------------------------------------------------===//

class FunctionCallAnalyzer {
private:
    std::set<std::string> kernel_functions = {
        "spin_lock", "spin_unlock", "spin_lock_irqsave", "spin_unlock_irqrestore",
        "mutex_lock", "mutex_unlock", "wake_up_interruptible", "wake_up",
        "netif_rx", "netif_receive_skb", "dev_kfree_skb", "alloc_skb",
        "printk", "pr_info", "pr_err", "pr_warn", "pr_debug",
        "kmalloc", "kfree", "vmalloc", "vfree",
        "ioremap", "iounmap", "readl", "writel", "readw", "writew"
    };
    
    FunctionPointerAnalyzer *fp_analyzer;
    
    /// 分析直接函数调用
    FunctionCallInfo analyzeDirectCall(CallInst *CI, Function *callee);
    
    /// 分析间接函数调用
    std::vector<FunctionCallInfo> analyzeIndirectCall(CallInst *CI);
    
    /// 检查是否是内核函数
    bool isKernelFunction(const std::string& func_name);
    
public:
    FunctionCallAnalyzer(FunctionPointerAnalyzer *fp_analyzer = nullptr) 
        : fp_analyzer(fp_analyzer) {}
    
    /// 设置函数指针分析器
    void setFunctionPointerAnalyzer(FunctionPointerAnalyzer *analyzer) {
        fp_analyzer = analyzer;
    }
    
    /// 分析函数中的所有函数调用
    std::vector<FunctionCallInfo> analyzeFunctionCalls(Function &F);
    
    /// 分析间接调用可能影响的所有内存访问
    std::vector<MemoryAccessInfo> getIndirectCallMemoryImpacts(Function &F);
};

#endif // IRQ_ANALYSIS_FUNCTION_CALL_ANALYZER_H
