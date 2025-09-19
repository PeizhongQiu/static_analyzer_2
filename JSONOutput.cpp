//===- JSONOutput.cpp - JSON Output Implementation ----------------------===//

#include "JSONOutput.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <ctime>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 辅助函数：检查是否应该过滤函数
//===----------------------------------------------------------------------===//

static bool shouldFilterFunctionCall(const std::string& name) {
    // LLVM内置函数
    if (name.find("llvm.") == 0) {
        return true;
    }
    
    // 编译器插桩函数
    static const std::vector<std::string> filter_prefixes = {
        "__sanitizer_cov_",
        "__asan_",
        "__msan_", 
        "__tsan_",
        "__ubsan_",
        "__gcov_",
        "__llvm_gcov_",
        "__llvm_gcda_",
        "__llvm_gcno_",
        "__coverage_",
        "__profile_",
        "__stack_chk_"
    };
    
    for (const auto& prefix : filter_prefixes) {
        if (name.find(prefix) == 0) {
            return true;
        }
    }
    
    return false;
}

json::Object JSONOutputGenerator::convertMemoryAccess(const MemoryAccessInfo& access) {
    json::Object access_obj;
    
    access_obj["type"] = (int64_t)access.type;
    access_obj["type_name"] = getAccessTypeName(access.type);
    access_obj["symbol_name"] = access.symbol_name;
    access_obj["struct_type_name"] = access.struct_type_name;
    access_obj["offset"] = access.offset;
    access_obj["access_size"] = (int64_t)access.access_size;
    access_obj["is_write"] = access.is_write;
    access_obj["is_atomic"] = access.is_atomic;
    access_obj["confidence"] = (int64_t)access.confidence;
    access_obj["source_location"] = access.source_location;
    
    // 指针链信息
    access_obj["chain_description"] = access.chain_description;
    
    // 详细的指针链元素
    json::Array chain_elements;
    for (const auto& elem : access.pointer_chain.elements) {
        json::Object elem_obj = convertPointerChainElement(elem);
        chain_elements.push_back(std::move(elem_obj));
    }
    access_obj["pointer_chain_elements"] = std::move(chain_elements);
    access_obj["chain_confidence"] = (int64_t)access.pointer_chain.confidence;
    access_obj["chain_is_complete"] = access.pointer_chain.is_complete;
    
    // 为fuzzing提供的额外信息
    access_obj["is_device_related"] = access.isDeviceRelatedAccess();
    access_obj["is_high_confidence"] = access.isHighConfidenceAccess();
    access_obj["fuzzing_target_description"] = access.getFuzzingTargetDescription();
    
    return access_obj;
}

json::Object JSONOutputGenerator::convertPointerChainElement(const PointerChainElement& elem) {
    json::Object elem_obj;
    
    elem_obj["type"] = (int64_t)elem.type;
    elem_obj["type_name"] = getPointerChainElementTypeName(elem.type);
    elem_obj["symbol_name"] = elem.symbol_name;
    elem_obj["struct_type_name"] = elem.struct_type_name;
    elem_obj["offset"] = elem.offset;
    
    // 为不同类型的元素提供特定的描述
    std::string description;
    switch (elem.type) {
        case PointerChainElement::GLOBAL_VAR_BASE:
            description = "Global variable: " + elem.symbol_name;
            break;
        case PointerChainElement::IRQ_HANDLER_ARG0:
            description = "IRQ number parameter (int irq)";
            break;
        case PointerChainElement::IRQ_HANDLER_ARG1:
            description = "Device ID parameter (void *dev_id)";
            break;
        case PointerChainElement::STRUCT_FIELD_DEREF:
            description = "Struct field access: " + elem.struct_type_name + 
                         " offset " + std::to_string(elem.offset);
            break;
        case PointerChainElement::ARRAY_INDEX_DEREF:
            description = "Array element access: index " + std::to_string(elem.offset);
            break;
        case PointerChainElement::CONSTANT_OFFSET:
            description = "Constant address: 0x" + std::to_string(elem.offset);
            break;
        default:
            description = "Direct load/store operation";
            break;
    }
    elem_obj["description"] = description;
    
    return elem_obj;
}

json::Object JSONOutputGenerator::convertRegisterAccess(const RegisterAccessInfo& reg_access) {
    json::Object reg_obj;
    
    reg_obj["register_name"] = reg_access.register_name;
    reg_obj["is_write"] = reg_access.is_write;
    reg_obj["inline_asm_constraint"] = reg_access.inline_asm_constraint;
    reg_obj["source_location"] = reg_access.source_location;
    
    return reg_obj;
}

json::Object JSONOutputGenerator::convertFunctionCall(const FunctionCallInfo& call) {
    json::Object call_obj;
    
    call_obj["callee_name"] = call.callee_name;
    call_obj["is_direct_call"] = call.is_direct_call;
    call_obj["is_kernel_function"] = call.is_kernel_function;
    call_obj["source_location"] = call.source_location;
    call_obj["confidence"] = (int64_t)call.confidence;
    call_obj["analysis_reason"] = call.analysis_reason;
    
    json::Array arg_types;
    for (const auto& arg_type : call.argument_types) {
        arg_types.push_back(arg_type);
    }
    call_obj["argument_types"] = std::move(arg_types);
    
    return call_obj;
}

json::Object JSONOutputGenerator::convertFunctionPointerTarget(const FunctionPointerTarget& target) {
    json::Object target_obj;
    
    target_obj["function_name"] = target.target_function->getName().str();
    target_obj["confidence"] = (int64_t)target.confidence;
    target_obj["analysis_reason"] = target.analysis_reason;
    
    return target_obj;
}

json::Object JSONOutputGenerator::convertIndirectCallAnalysis(const IndirectCallAnalysis& indirect) {
    json::Object indirect_obj;
    
    // 函数指针信息
    json::Object fp_info;
    fp_info["pointer_name"] = indirect.fp_analysis.pointer_name;
    fp_info["is_resolved"] = indirect.fp_analysis.is_resolved;
    
    json::Array targets;
    for (const auto& target : indirect.fp_analysis.possible_targets) {
        // 过滤LLVM内置函数目标
        std::string target_name = target.target_function->getName().str();
        if (!shouldFilterFunctionCall(target_name)) {
            json::Object target_obj = convertFunctionPointerTarget(target);
            targets.push_back(std::move(target_obj));
        }
    }
    fp_info["possible_targets"] = std::move(targets);
    
    // 统计信息（基于过滤后的结果）
    fp_info["total_targets"] = (int64_t)indirect.getTotalPossibleTargets();
    fp_info["high_confidence_targets"] = (int64_t)indirect.getHighConfidenceTargets();
    
    indirect_obj["function_pointer_analysis"] = std::move(fp_info);
    
    // 聚合的内存访问影响
    json::Array aggregated_accesses;
    for (const auto& access : indirect.aggregated_accesses) {
        json::Object access_obj = convertMemoryAccess(access);
        aggregated_accesses.push_back(std::move(access_obj));
    }
    indirect_obj["aggregated_memory_accesses"] = std::move(aggregated_accesses);
    
    // 聚合的寄存器访问
    json::Array aggregated_registers;
    for (const auto& reg_access : indirect.aggregated_register_accesses) {
        json::Object reg_obj = convertRegisterAccess(reg_access);
        aggregated_registers.push_back(std::move(reg_obj));
    }
    indirect_obj["aggregated_register_accesses"] = std::move(aggregated_registers);
    
    return indirect_obj;
}

json::Object JSONOutputGenerator::convertHandlerAnalysis(const InterruptHandlerAnalysis& analysis) {
    json::Object handler_obj;
    
    handler_obj["function_name"] = analysis.function_name;
    handler_obj["source_file"] = analysis.source_file;
    handler_obj["line_number"] = (int64_t)analysis.line_number;
    handler_obj["is_confirmed_irq_handler"] = analysis.is_confirmed_irq_handler;
    handler_obj["basic_block_count"] = (int64_t)analysis.basic_block_count;
    handler_obj["loop_count"] = (int64_t)analysis.loop_count;
    handler_obj["has_recursive_calls"] = analysis.has_recursive_calls;
    
    // 直接内存访问信息
    json::Array memory_accesses;
    for (const auto& access : analysis.memory_accesses) {
        json::Object access_obj = convertMemoryAccess(access);
        memory_accesses.push_back(std::move(access_obj));
    }
    handler_obj["memory_accesses"] = std::move(memory_accesses);
    
    // 寄存器访问信息
    json::Array register_accesses;
    for (const auto& reg_access : analysis.register_accesses) {
        json::Object reg_obj = convertRegisterAccess(reg_access);
        register_accesses.push_back(std::move(reg_obj));
    }
    handler_obj["register_accesses"] = std::move(register_accesses);
    
    // 函数调用信息 - 过滤LLVM内置函数
    json::Array function_calls;
    int filtered_count = 0;
    for (const auto& call : analysis.function_calls) {
        if (shouldFilterFunctionCall(call.callee_name)) {
            filtered_count++;
            continue; // 跳过LLVM内置函数
        }
        json::Object call_obj = convertFunctionCall(call);
        function_calls.push_back(std::move(call_obj));
    }
    handler_obj["function_calls"] = std::move(function_calls);
    
    // 添加过滤统计信息
    if (filtered_count > 0) {
        handler_obj["filtered_intrinsic_calls"] = filtered_count;
    }
    
    // 间接调用详细分析
    json::Array indirect_calls;
    for (const auto& indirect : analysis.indirect_call_analyses) {
        json::Object indirect_obj = convertIndirectCallAnalysis(indirect);
        indirect_calls.push_back(std::move(indirect_obj));
    }
    handler_obj["indirect_call_analyses"] = std::move(indirect_calls);
    
    // 总内存访问（包含间接调用影响）
    json::Array total_accesses;
    for (const auto& access : analysis.total_memory_accesses) {
        json::Object access_obj = convertMemoryAccess(access);
        total_accesses.push_back(std::move(access_obj));
    }
    handler_obj["total_memory_accesses"] = std::move(total_accesses);
    
    // 访问的全局变量
    json::Array global_vars;
    for (const auto& var : analysis.accessed_global_vars) {
        global_vars.push_back(var);
    }
    handler_obj["accessed_global_vars"] = std::move(global_vars);
    
    // 访问的结构体类型
    json::Array struct_types;
    for (const auto& type : analysis.accessed_struct_types) {
        struct_types.push_back(type);
    }
    handler_obj["accessed_struct_types"] = std::move(struct_types);
    
    // 为fuzzing提供的汇总信息
    json::Object fuzzing_summary;
    
    // 统计不同类型的访问
    int dev_id_accesses = 0;
    int global_accesses = 0;
    int high_confidence_writes = 0;
    int meaningful_calls = 0;
    
    for (const auto& access : analysis.total_memory_accesses) {
        if (access.isDeviceRelatedAccess()) {
            dev_id_accesses++;
        }
        if (access.type == MemoryAccessInfo::GLOBAL_VARIABLE) {
            global_accesses++;
        }
        if (access.isHighConfidenceAccess() && access.is_write) {
            high_confidence_writes++;
        }
    }
    
    // 计算有意义的函数调用数
    for (const auto& call : analysis.function_calls) {
        if (!shouldFilterFunctionCall(call.callee_name)) {
            meaningful_calls++;
        }
    }
    
    fuzzing_summary["dev_id_related_accesses"] = dev_id_accesses;
    fuzzing_summary["global_variable_accesses"] = global_accesses;
    fuzzing_summary["high_confidence_writes"] = high_confidence_writes;
    fuzzing_summary["meaningful_function_calls"] = meaningful_calls;
    fuzzing_summary["total_indirect_calls"] = (int64_t)analysis.indirect_call_analyses.size();
    
    // 推荐的fuzzing优先级
    std::string priority = "LOW";
    if (high_confidence_writes > 3 || dev_id_accesses > 5 || meaningful_calls > 10) {
        priority = "HIGH";
    } else if (high_confidence_writes > 1 || dev_id_accesses > 2 || meaningful_calls > 5) {
        priority = "MEDIUM";
    }
    fuzzing_summary["recommended_fuzzing_priority"] = priority;
    
    handler_obj["fuzzing_summary"] = std::move(fuzzing_summary);
    
    return handler_obj;
}

json::Value JSONOutputGenerator::convertToJSON(const std::vector<InterruptHandlerAnalysis>& results) {
    json::Object output;
    json::Array handlers_array;
    
    for (const auto& analysis : results) {
        json::Object handler_obj = convertHandlerAnalysis(analysis);
        handlers_array.push_back(std::move(handler_obj));
    }
    
    output["interrupt_handlers"] = std::move(handlers_array);
    output["analysis_timestamp"] = (int64_t)std::time(nullptr);
    output["total_handlers_found"] = (int64_t)results.size();
    
    // 全局统计信息 - 过滤后的统计
    json::Object global_stats;
    int total_memory_accesses = 0;
    int total_dev_id_accesses = 0;
    int total_meaningful_calls = 0;  // 替代 total_function_calls
    int total_indirect_calls = 0;
    int total_filtered_calls = 0;
    
    for (const auto& analysis : results) {
        total_memory_accesses += analysis.total_memory_accesses.size();
        total_indirect_calls += analysis.indirect_call_analyses.size();
        
        for (const auto& access : analysis.total_memory_accesses) {
            if (access.isDeviceRelatedAccess()) {
                total_dev_id_accesses++;
            }
        }
        
        // 计算有意义的函数调用
        for (const auto& call : analysis.function_calls) {
            if (shouldFilterFunctionCall(call.callee_name)) {
                total_filtered_calls++;
            } else {
                total_meaningful_calls++;
            }
        }
    }
    
    global_stats["total_memory_accesses"] = total_memory_accesses;
    global_stats["total_dev_id_accesses"] = total_dev_id_accesses;
    global_stats["total_meaningful_function_calls"] = total_meaningful_calls;
    global_stats["total_indirect_calls"] = total_indirect_calls;
    global_stats["total_filtered_intrinsic_calls"] = total_filtered_calls;
    
    output["global_statistics"] = std::move(global_stats);
    
    // 添加过滤信息说明
    json::Object filter_info;
    filter_info["description"] = "LLVM intrinsic and instrumentation functions have been filtered out";
    filter_info["filtered_prefixes"] = json::Array{
        "llvm.", "__sanitizer_cov_", "__asan_", "__gcov_", "__llvm_gcov_"
    };
    output["filtering_applied"] = std::move(filter_info);
    
    return json::Value(std::move(output));
}

void JSONOutputGenerator::outputAnalysisResults(const std::vector<InterruptHandlerAnalysis>& results,
                                               const std::string& output_file) {
    json::Value json_output = convertToJSON(results);
    
    std::error_code EC;
    raw_fd_ostream OS(output_file, EC);
    if (EC) {
        errs() << "Error opening output file: " << EC.message() << "\n";
        return;
    }
    
    OS << formatv("{0:2}", json_output) << "\n";
    outs() << "Analysis results written to: " << output_file << "\n";
    
    // 显示过滤统计
    if (results.size() > 0) {
        int total_filtered = 0;
        int total_meaningful = 0;
        
        for (const auto& analysis : results) {
            for (const auto& call : analysis.function_calls) {
                if (shouldFilterFunctionCall(call.callee_name)) {
                    total_filtered++;
                } else {
                    total_meaningful++;
                }
            }
        }
        
        outs() << "Function call filtering summary:\n";
        outs() << "  Meaningful calls: " << total_meaningful << "\n";
        outs() << "  Filtered intrinsics: " << total_filtered << "\n";
    }
}
