//===- FunctionCallAnalyzer.cpp - 函数调用分析器实现 ---------------------===//

#include "SVFInterruptAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 详细函数调用分析
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::analyzeFunctionCalls(Function* handler, InterruptHandlerResult& result) {
    std::map<std::string, FunctionCallInfo> call_map;
    
    for (auto& BB : *handler) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                result.function_calls++;
                
                FunctionCallInfo call_info;
                call_info.call_sites.push_back(getInstructionLocation(&I));
                
                if (CI->getCalledFunction()) {
                    // 直接调用
                    std::string func_name = CI->getCalledFunction()->getName().str();
                    
                    // 过滤掉调试内联函数
                    if (isDebugIntrinsic(func_name)) {
                        continue;
                    }
                    
                    call_info.function_name = func_name;
                    call_info.call_type = "direct";
                    
                    result.direct_function_calls.push_back(func_name);
                    
                    // 分析函数特性
                    analyzeCalleeCharacteristics(CI->getCalledFunction(), call_info);
                    
                } else {
                    // 间接调用
                    result.indirect_calls++;
                    call_info.function_name = "indirect_call_" + std::to_string(result.indirect_calls);
                    call_info.call_type = "indirect";
                    
                    // 尝试解析间接调用目标
                    Value* called_value = CI->getCalledOperand();
                    call_info.possible_targets = resolveFunctionPointer(called_value);
                    
                    for (const auto& target : call_info.possible_targets) {
                        result.indirect_call_targets.push_back(target);
                    }
                    
                    // 分析间接调用模式
                    analyzeIndirectCallPattern(CI, call_info);
                }
                
                // 合并调用信息
                mergeCallInfo(call_map, call_info);
            } else if (auto* II = dyn_cast<InvokeInst>(&I)) {
                // 处理invoke指令（异常处理相关）
                handleInvokeInstruction(II, result, call_map);
            }
        }
    }
    
    // 转换为vector并排序
    convertAndSortCallInfo(call_map, result);
    
    // 分析调用模式
    analyzeCallPatterns(result);
}

void SVFInterruptAnalyzer::mergeCallInfo(std::map<std::string, FunctionCallInfo>& call_map, 
                                        const FunctionCallInfo& call_info) {
    auto& existing = call_map[call_info.function_name];
    existing.function_name = call_info.function_name;
    existing.call_type = call_info.call_type;
    existing.call_count++;
    
    // 合并调用点
    existing.call_sites.insert(existing.call_sites.end(), 
                              call_info.call_sites.begin(), 
                              call_info.call_sites.end());
    
    // 合并可能的目标
    existing.possible_targets.insert(existing.possible_targets.end(),
                                    call_info.possible_targets.begin(),
                                    call_info.possible_targets.end());
    
    // 去重possible_targets
    std::sort(existing.possible_targets.begin(), existing.possible_targets.end());
    existing.possible_targets.erase(
        std::unique(existing.possible_targets.begin(), existing.possible_targets.end()),
        existing.possible_targets.end());
}

void SVFInterruptAnalyzer::convertAndSortCallInfo(const std::map<std::string, FunctionCallInfo>& call_map,
                                                 InterruptHandlerResult& result) {
    // 转换为vector
    for (const auto& pair : call_map) {
        result.function_call_details.push_back(pair.second);
    }
    
    // 按调用次数排序
    std::sort(result.function_call_details.begin(), result.function_call_details.end(),
              [](const FunctionCallInfo& a, const FunctionCallInfo& b) {
                  return a.call_count > b.call_count;
              });
}

//===----------------------------------------------------------------------===//
// 函数特性分析
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::analyzeCalleeCharacteristics(Function* callee, FunctionCallInfo& call_info) {
    if (!callee) return;
    
    std::string func_name = callee->getName().str();
    
    // 分析函数类型
    if (isInterruptRelatedFunction(func_name)) {
        call_info.call_type += "_irq_related";
    } else if (isDeviceRelatedFunction(func_name)) {
        call_info.call_type += "_device_related";
    } else if (isMemoryRelatedFunction(func_name)) {
        call_info.call_type += "_memory_related";
    } else if (isLockingFunction(func_name)) {
        call_info.call_type += "_locking";
    } else if (isWorkQueueFunction(func_name)) {
        call_info.call_type += "_workqueue";
    }
    
    // 分析函数参数和返回值
    analyzeCallSignature(callee, call_info);
}

void SVFInterruptAnalyzer::analyzeCallSignature(Function* callee, FunctionCallInfo& call_info) {
    if (!callee) return;
    
    // 分析参数数量和类型
    unsigned arg_count = callee->arg_size();
    if (arg_count > 5) {
        call_info.call_type += "_many_args";
    }
    
    // 分析返回值类型
    Type* ret_type = callee->getReturnType();
    if (ret_type->isPointerTy()) {
        call_info.call_type += "_returns_pointer";
    } else if (ret_type->isIntegerTy()) {
        call_info.call_type += "_returns_int";
    }
}

void SVFInterruptAnalyzer::analyzeIndirectCallPattern(CallInst* CI, FunctionCallInfo& call_info) {
    Value* called_value = CI->getCalledOperand();
    
    // 分析间接调用的来源
    if (auto* arg = dyn_cast<Argument>(called_value)) {
        call_info.call_type += "_function_parameter";
    } else if (auto* load = dyn_cast<LoadInst>(called_value)) {
        call_info.call_type += "_loaded_function_pointer";
        
        // 进一步分析加载的来源
        Value* ptr = load->getPointerOperand();
        if (isa<GlobalVariable>(ptr)) {
            call_info.call_type += "_global";
        } else if (auto* gep = dyn_cast<GetElementPtrInst>(ptr)) {
            call_info.call_type += "_struct_field";
        }
    } else if (auto* gep = dyn_cast<GetElementPtrInst>(called_value)) {
        call_info.call_type += "_struct_function_pointer";
    }
}

//===----------------------------------------------------------------------===//
// 调用模式分析
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::analyzeCallPatterns(InterruptHandlerResult& result) {
    // 分析调用频率模式
    size_t total_calls = 0;
    for (const auto& call_info : result.function_call_details) {
        total_calls += call_info.call_count;
    }
    
    // 标识高频调用
    for (auto& call_info : result.function_call_details) {
        double call_ratio = static_cast<double>(call_info.call_count) / total_calls;
        if (call_ratio > 0.2) {  // 超过20%的调用
            call_info.call_type += "_high_frequency";
        }
    }
    
    // 分析调用链模式
    analyzeCallChainPatterns(result);
    
    // 分析异常处理调用
    analyzeExceptionHandlingCalls(result);
}

void SVFInterruptAnalyzer::analyzeCallChainPatterns(InterruptHandlerResult& result) {
    // 简化的调用链分析
    // 在更复杂的实现中，可以构建调用图来分析调用模式
    
    std::set<std::string> critical_functions = {
        "schedule", "wake_up", "complete", "kfree", "kmalloc",
        "spin_lock", "spin_unlock", "mutex_lock", "mutex_unlock"
    };
    
    for (auto& call_info : result.function_call_details) {
        if (critical_functions.find(call_info.function_name) != critical_functions.end()) {
            call_info.call_type += "_critical";
        }
    }
}

void SVFInterruptAnalyzer::analyzeExceptionHandlingCalls(InterruptHandlerResult& result) {
    std::set<std::string> exception_functions = {
        "__cxa_throw", "__cxa_begin_catch", "__cxa_end_catch",
        "panic", "BUG", "WARN_ON"
    };
    
    for (auto& call_info : result.function_call_details) {
        if (exception_functions.find(call_info.function_name) != exception_functions.end()) {
            call_info.call_type += "_exception_handling";
        }
    }
}

//===----------------------------------------------------------------------===//
// 辅助函数
//===----------------------------------------------------------------------===//

bool SVFInterruptAnalyzer::isDebugIntrinsic(const std::string& name) {
    return name.find("llvm.dbg.") == 0 || 
           name.find("llvm.lifetime.") == 0 ||
           name.find("__sanitizer_") == 0;
}

bool SVFInterruptAnalyzer::isMemoryRelatedFunction(const std::string& name) {
    static const std::vector<std::string> keywords = {
        "kmalloc", "kfree", "vmalloc", "vfree", "memcpy", "memset",
        "copy_from_user", "copy_to_user", "get_user", "put_user"
    };
    
    for (const auto& keyword : keywords) {
        if (name.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool SVFInterruptAnalyzer::isLockingFunction(const std::string& name) {
    static const std::vector<std::string> keywords = {
        "spin_lock", "spin_unlock", "mutex_lock", "mutex_unlock",
        "down", "up", "semaphore", "rwlock", "rcu_read"
    };
    
    for (const auto& keyword : keywords) {
        if (name.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool SVFInterruptAnalyzer::isWorkQueueFunction(const std::string& name) {
    static const std::vector<std::string> keywords = {
        "queue_work", "schedule_work", "flush_work", "cancel_work",
        "schedule_delayed_work", "mod_delayed_work"
    };
    
    for (const auto& keyword : keywords) {
        if (name.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void SVFInterruptAnalyzer::handleInvokeInstruction(InvokeInst* II, 
                                                  InterruptHandlerResult& result,
                                                  std::map<std::string, FunctionCallInfo>& call_map) {
    // Invoke指令处理（用于异常处理）
    result.function_calls++;
    
    FunctionCallInfo call_info;
    call_info.call_sites.push_back(getInstructionLocation(II));
    
    if (II->getCalledFunction()) {
        std::string func_name = II->getCalledFunction()->getName().str();
        call_info.function_name = func_name;
        call_info.call_type = "invoke_direct";
        
        result.direct_function_calls.push_back(func_name);
    } else {
        result.indirect_calls++;
        call_info.function_name = "invoke_indirect";
        call_info.call_type = "invoke_indirect";
    }
    
    mergeCallInfo(call_map, call_info);
}
