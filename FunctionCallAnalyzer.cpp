//===- FunctionCallAnalyzer.cpp - Function Call Analyzer Implementation ===//

#include "FunctionCallAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

std::vector<FunctionCallInfo> FunctionCallAnalyzer::analyzeFunctionCalls(Function &F) {
    std::vector<FunctionCallInfo> calls;
    
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *CI = dyn_cast<CallInst>(&I)) {
                if (Function *callee = CI->getCalledFunction()) {
                    // 直接函数调用
                    FunctionCallInfo info = analyzeDirectCall(CI, callee);
                    calls.push_back(info);
                } else {
                    // 间接函数调用 - 需要函数指针分析
                    std::vector<FunctionCallInfo> indirect_calls = analyzeIndirectCall(CI);
                    calls.insert(calls.end(), indirect_calls.begin(), indirect_calls.end());
                }
            }
        }
    }
    
    return calls;
}

FunctionCallInfo FunctionCallAnalyzer::analyzeDirectCall(CallInst *CI, Function *callee) {
    FunctionCallInfo info;
    
    info.callee_name = callee->getName().str();
    info.is_direct_call = true;
    info.is_kernel_function = isKernelFunction(info.callee_name);
    info.confidence = 100;
    info.analysis_reason = "direct_call";
    
    // 分析参数类型
    for (unsigned i = 0; i < CI->getNumOperands() - 1; ++i) {
        Value *arg = CI->getOperand(i);
        info.argument_types.push_back(std::to_string(arg->getType()->getTypeID()));
    }
    
    // 添加源码位置 - 修复DebugLoc处理
    const DebugLoc &DI = CI->getDebugLoc();
    if (DI) {
        info.source_location = DI->getFilename().str() + ":" + 
                             std::to_string(DI->getLine());
    }
    
    return info;
}

std::vector<FunctionCallInfo> FunctionCallAnalyzer::analyzeIndirectCall(CallInst *CI) {
    std::vector<FunctionCallInfo> calls;
    
    if (!fp_analyzer) {
        // 没有函数指针分析器，只能记录为间接调用
        FunctionCallInfo info;
        info.callee_name = "indirect_call_unknown";
        info.is_direct_call = false;
        info.confidence = 20;
        info.analysis_reason = "no_function_pointer_analyzer";
        calls.push_back(info);
        return calls;
    }
    
    // 使用函数指针分析器分析可能的目标
    FunctionPointerAnalysis fp_analysis = fp_analyzer->analyzeFunctionPointer(CI->getCalledOperand());
    
    if (fp_analysis.possible_targets.empty()) {
        // 没有找到可能的目标
        FunctionCallInfo info;
        info.callee_name = "indirect_call_unresolved";
        info.is_direct_call = false;
        info.confidence = 10;
        info.analysis_reason = "unresolved_function_pointer";
        
        // 添加源码位置 - 修复DebugLoc处理
        const DebugLoc &DI = CI->getDebugLoc();
        if (DI) {
            info.source_location = DI->getFilename().str() + ":" + 
                                 std::to_string(DI->getLine());
        }
        
        calls.push_back(info);
        return calls;
    }
    
    // 为每个可能的目标创建函数调用信息
    for (const auto &target : fp_analysis.possible_targets) {
        FunctionCallInfo info;
        info.callee_name = target.target_function->getName().str();
        info.is_direct_call = false;
        info.is_kernel_function = isKernelFunction(info.callee_name);
        info.confidence = target.confidence;
        info.analysis_reason = target.analysis_reason;
        
        // 分析参数类型
        for (unsigned i = 0; i < CI->getNumOperands() - 1; ++i) {
            Value *arg = CI->getOperand(i);
            info.argument_types.push_back(std::to_string(arg->getType()->getTypeID()));
        }
        
        // 添加源码位置 - 修复DebugLoc处理
        const DebugLoc &DI = CI->getDebugLoc();
        if (DI) {
            info.source_location = DI->getFilename().str() + ":" + 
                                 std::to_string(DI->getLine());
        }
        
        calls.push_back(info);
    }
    
    return calls;
}

std::vector<MemoryAccessInfo> FunctionCallAnalyzer::getIndirectCallMemoryImpacts(Function &F) {
    std::vector<MemoryAccessInfo> all_impacts;
    
    if (!fp_analyzer) return all_impacts;
    
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *CI = dyn_cast<CallInst>(&I)) {
                if (!CI->getCalledFunction()) {
                    // 间接调用
                    IndirectCallAnalysis analysis = fp_analyzer->analyzeIndirectCall(CI);
                    
                    all_impacts.insert(all_impacts.end(),
                                     analysis.aggregated_accesses.begin(),
                                     analysis.aggregated_accesses.end());
                }
            }
        }
    }
    
    return all_impacts;
}

bool FunctionCallAnalyzer::isKernelFunction(const std::string& func_name) {
    // 检查预定义的内核函数列表
    for (const auto& kfunc : kernel_functions) {
        if (func_name.find(kfunc) != std::string::npos) {
            return true;
        }
    }
    
    // 检查一些常见的内核函数模式 - 使用兼容的字符串方法
    return (func_name.size() >= 2 && func_name.substr(0, 2) == "__") ||        // 内核内部函数
           (func_name.size() >= 4 && func_name.substr(0, 4) == "sys_") ||      // 系统调用
           (func_name.size() >= 3 && func_name.substr(0, 3) == "do_") ||       // 内核do_函数
           (func_name.find("_lock") != std::string::npos) ||        // 锁相关函数
           (func_name.find("alloc") != std::string::npos) ||        // 内存分配函数
           (func_name.find("free") != std::string::npos) ||         // 内存释放函数
           (func_name.size() >= 4 && func_name.substr(0, 4) == "get_") ||      // getter函数
           (func_name.size() >= 4 && func_name.substr(0, 4) == "put_") ||      // putter函数
           (func_name.size() >= 5 && func_name.substr(0, 5) == "find_") ||     // 查找函数
           (func_name.size() >= 5 && func_name.substr(0, 5) == "init_") ||     // 初始化函数
           (func_name.size() >= 5 && func_name.substr(0, 5) == "exit_");       // 退出函数
}
