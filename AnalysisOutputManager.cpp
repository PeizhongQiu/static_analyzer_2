//===- AnalysisOutputManager.cpp - 分析结果输出管理器实现 -----------------===//

#include "SVFInterruptAnalyzer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/JSON.h"
#include <fstream>
#include <ctime>

#ifdef SVF_AVAILABLE
#include "SVF-LLVM/LLVMModule.h"
#endif

using namespace llvm;

//===----------------------------------------------------------------------===//
// 置信度计算
//===----------------------------------------------------------------------===//

double SVFInterruptAnalyzer::calculateConfidence(const InterruptHandlerResult& result) {
    double score = 30.0;  // 基础分数
    
    // 分析完整性加分
    if (result.analysis_complete) score += 10.0;
    
    // 内存操作分析质量
    if (result.memory_write_operations > 0) score += 15.0;
    if (result.memory_read_operations > 0) score += 10.0;
    
    // 数据结构分析质量 - 增强评分
    if (!result.data_structure_accesses.empty()) score += 15.0;
    if (result.data_structure_accesses.size() > 3) score += 5.0;
    
    // 新增：结构体信息质量评分
    int struct_fields_with_real_names = 0;
    int struct_fields_total = 0;
    
    for (const auto& write_op : result.memory_writes) {
        if (write_op.target_type == "struct_field") {
            struct_fields_total++;
            // 检查是否有真实字段名（而不是field_N格式）
            if (!write_op.field_name.empty() && 
                write_op.field_name.find("field_") != 0) {
                struct_fields_with_real_names++;
            }
            // 检查是否有完整的结构体信息
            if (!write_op.struct_name.empty() && write_op.field_offset > 0) {
                score += 2.0;
            }
        }
    }
    
    // 根据真实字段名比例加分
    if (struct_fields_total > 0) {
        double real_name_ratio = (double)struct_fields_with_real_names / struct_fields_total;
        score += real_name_ratio * 10.0; // 最多10分
    }
    
    // 函数调用分析质量
    if (!result.function_call_details.empty()) score += 10.0;
    if (!result.direct_function_calls.empty()) score += 5.0;
    
    // 全局变量修改分析
    if (!result.modified_global_vars.empty()) score += 10.0;
    if (!result.modified_static_vars.empty()) score += 5.0;
    
    // 函数指针分析
    if (!result.function_pointer_targets.empty()) score += 10.0;
    
    // 复杂度加分
    if (result.total_instructions > 50) score += 5.0;
    if (result.total_instructions > 100) score += 5.0;
    
    // 分析深度加分 - 更新判断条件
    if (hasAdvancedAnalysisFeatures(result)) score += 5.0;
    
    return std::min(score, 100.0);
}

bool SVFInterruptAnalyzer::hasAdvancedAnalysisFeatures(const InterruptHandlerResult& result) {
    // 更新判断条件，考虑增强的结构体信息
    bool has_detailed_struct_info = false;
    for (const auto& write_op : result.memory_writes) {
        if (write_op.target_type == "struct_field" && 
            !write_op.struct_name.empty() && 
            write_op.field_offset > 0) {
            has_detailed_struct_info = true;
            break;
        }
    }
    
    return !result.memory_writes.empty() &&
           !result.data_structure_accesses.empty() &&
           !result.function_call_details.empty() &&
           has_detailed_struct_info;
}

//===----------------------------------------------------------------------===//
// JSON输出生成
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::outputResults(const std::vector<InterruptHandlerResult>& results, 
                                         const std::string& output_file) {
    json::Object root;
    json::Array handlers;
    
    for (const auto& result : results) {
        json::Object handler = createHandlerJson(result);
        handlers.push_back(std::move(handler));
    }
    
    root["interrupt_handlers"] = std::move(handlers);
    root["total_handlers"] = (int64_t)results.size();
    root["analysis_timestamp"] = (int64_t)std::time(nullptr);
    root["analyzer_version"] = "Enhanced-SVF-2.0";
    
    // 增强的统计信息
    json::Object stats = createStatisticsJson(results);
    root["enhanced_statistics"] = std::move(stats);
    
    // 写入文件
    writeJsonToFile(root, output_file);
}

json::Object SVFInterruptAnalyzer::createHandlerJson(const InterruptHandlerResult& result) {
    json::Object handler;
    
    // 基础信息
    addBasicInfo(handler, result);
    
    // 增强的内存操作信息
    addMemoryOperationInfo(handler, result);
    
    // 数据结构访问
    addDataStructureInfo(handler, result);
    
    // 详细函数调用信息
    addFunctionCallInfo(handler, result);
    
    // 内存写操作详情
    addMemoryWriteInfo(handler, result);
    
    // 修改的变量
    addVariableModificationInfo(handler, result);
    
    // 函数指针目标
    addFunctionPointerInfo(handler, result);
    
    // 特征标记
    addFeatureFlags(handler, result);
    
    return handler;
}

void SVFInterruptAnalyzer::addBasicInfo(json::Object& handler, const InterruptHandlerResult& result) {
    handler["function_name"] = result.function_name;
    handler["source_file"] = result.source_file;
    handler["module_file"] = result.module_file;
    handler["total_instructions"] = (int64_t)result.total_instructions;
    handler["total_basic_blocks"] = (int64_t)result.total_basic_blocks;
    handler["function_calls"] = (int64_t)result.function_calls;
    handler["indirect_calls"] = (int64_t)result.indirect_calls;
}

void SVFInterruptAnalyzer::addMemoryOperationInfo(json::Object& handler, const InterruptHandlerResult& result) {
    handler["memory_read_operations"] = (int64_t)result.memory_read_operations;
    handler["memory_write_operations"] = (int64_t)result.memory_write_operations;
}

void SVFInterruptAnalyzer::addDataStructureInfo(json::Object& handler, const InterruptHandlerResult& result) {
    json::Array data_structures;
    for (const auto& access : result.data_structure_accesses) {
        json::Object ds_obj;
        
        // 清理结构体名称 - 只保留主要部分
        std::string clean_struct_name = access.struct_name;
        
        // 移除尾部的数字标识符 (如 .19, .15 等)
        size_t dot_pos = clean_struct_name.find_last_of('.');
        if (dot_pos != std::string::npos) {
            std::string suffix = clean_struct_name.substr(dot_pos + 1);
            bool is_numeric = !suffix.empty() && 
                std::all_of(suffix.begin(), suffix.end(), ::isdigit);
            if (is_numeric) {
                clean_struct_name = clean_struct_name.substr(0, dot_pos);
            }
        }
        
        ds_obj["struct_name"] = clean_struct_name;
        ds_obj["field_name"] = access.field_name;
        ds_obj["field_type"] = access.field_type;
        ds_obj["offset"] = (int64_t)access.offset;
        ds_obj["is_pointer_field"] = access.is_pointer_field;
        ds_obj["access_pattern"] = access.access_pattern;
        
        data_structures.push_back(std::move(ds_obj));
    }
    handler["data_structure_accesses"] = std::move(data_structures);
}

void SVFInterruptAnalyzer::addFunctionCallInfo(json::Object& handler, const InterruptHandlerResult& result) {
    json::Array function_calls_detail;
    for (const auto& call_info : result.function_call_details) {
        json::Object call_obj;
        call_obj["function_name"] = call_info.function_name;
        call_obj["call_type"] = call_info.call_type;
        call_obj["call_count"] = (int64_t)call_info.call_count;
        
        json::Array call_sites;
        for (const auto& site : call_info.call_sites) {
            call_sites.push_back(site);
        }
        call_obj["call_sites"] = std::move(call_sites);
        
        json::Array targets;
        for (const auto& target : call_info.possible_targets) {
            targets.push_back(target);
        }
        call_obj["possible_targets"] = std::move(targets);
        
        function_calls_detail.push_back(std::move(call_obj));
    }
    handler["function_call_details"] = std::move(function_calls_detail);
}

void SVFInterruptAnalyzer::addMemoryWriteInfo(json::Object& handler, const InterruptHandlerResult& result) {
    json::Array memory_writes;
    for (const auto& write_op : result.memory_writes) {
        json::Object write_obj;
        write_obj["target_name"] = write_op.target_name;
        write_obj["target_type"] = write_op.target_type;
        write_obj["data_type"] = write_op.data_type;
        write_obj["write_count"] = (int64_t)write_op.write_count;
        write_obj["is_critical"] = write_op.is_critical;
        
        json::Array locations;
        for (const auto& loc : write_op.write_locations) {
            locations.push_back(loc);
        }
        write_obj["write_locations"] = std::move(locations);
        
        // 新增：结构体相关信息
        if (write_op.target_type == "struct_field" && !write_op.struct_name.empty()) {
            json::Object struct_info;
            struct_info["struct_name"] = write_op.struct_name;
            struct_info["field_name"] = write_op.field_name;
            struct_info["field_type"] = write_op.field_type;
            struct_info["field_offset"] = (int64_t)write_op.field_offset;
            struct_info["field_size"] = (int64_t)write_op.field_size;
            struct_info["full_path"] = write_op.full_path;
            
            write_obj["struct_info"] = std::move(struct_info);
        }
        
        // 新增：数组相关信息
        if (write_op.target_type == "array_element") {
            json::Object array_info;
            array_info["field_name"] = write_op.field_name;
            array_info["field_offset"] = (int64_t)write_op.field_offset;
            array_info["field_size"] = (int64_t)write_op.field_size;
            array_info["full_path"] = write_op.full_path;
            
            write_obj["array_info"] = std::move(array_info);
        }
        
        memory_writes.push_back(std::move(write_obj));
    }
    handler["memory_writes"] = std::move(memory_writes);
}

void SVFInterruptAnalyzer::addVariableModificationInfo(json::Object& handler, const InterruptHandlerResult& result) {
    json::Array modified_globals;
    for (const auto& var : result.modified_global_vars) {
        modified_globals.push_back(var);
    }
    handler["modified_global_vars"] = std::move(modified_globals);
    
    json::Array modified_statics;
    for (const auto& var : result.modified_static_vars) {
        modified_statics.push_back(var);
    }
    handler["modified_static_vars"] = std::move(modified_statics);
}

void SVFInterruptAnalyzer::addFunctionPointerInfo(json::Object& handler, const InterruptHandlerResult& result) {
    json::Object func_ptr_targets;
    for (const auto& pair : result.function_pointer_targets) {
        json::Array targets;
        for (const auto& target : pair.second) {
            targets.push_back(target);
        }
        func_ptr_targets[pair.first] = std::move(targets);
    }
    handler["function_pointer_targets"] = std::move(func_ptr_targets);
    
    json::Array direct_calls;
    for (const auto& call : result.direct_function_calls) {
        direct_calls.push_back(call);
    }
    handler["direct_function_calls"] = std::move(direct_calls);
    
    json::Array indirect_targets;
    for (const auto& target : result.indirect_call_targets) {
        indirect_targets.push_back(target);
    }
    handler["indirect_call_targets"] = std::move(indirect_targets);
}

void SVFInterruptAnalyzer::addFeatureFlags(json::Object& handler, const InterruptHandlerResult& result) {
    handler["has_device_access"] = result.has_device_access;
    handler["has_irq_operations"] = result.has_irq_operations;
    handler["has_work_queue_ops"] = result.has_work_queue_ops;
    handler["analysis_complete"] = result.analysis_complete;
    handler["confidence_score"] = result.confidence_score;
}

json::Object SVFInterruptAnalyzer::createStatisticsJson(const std::vector<InterruptHandlerResult>& results) {
    json::Object stats;
    
    size_t successful = 0;
    size_t with_data_struct_access = 0;
    size_t with_global_writes = 0;
    size_t with_func_pointers = 0;
    size_t total_memory_writes = 0;
    size_t total_memory_reads = 0;
    double avg_confidence = 0.0;
    
    for (const auto& result : results) {
        if (result.analysis_complete) successful++;
        if (!result.data_structure_accesses.empty()) with_data_struct_access++;
        if (!result.modified_global_vars.empty()) with_global_writes++;
        if (!result.function_pointer_targets.empty()) with_func_pointers++;
        total_memory_writes += result.memory_write_operations;
        total_memory_reads += result.memory_read_operations;
        avg_confidence += result.confidence_score;
    }
    
    if (!results.empty()) avg_confidence /= results.size();
    
    stats["successful_analyses"] = (int64_t)successful;
    stats["handlers_with_data_structure_access"] = (int64_t)with_data_struct_access;
    stats["handlers_with_global_writes"] = (int64_t)with_global_writes;
    stats["handlers_with_function_pointers"] = (int64_t)with_func_pointers;
    stats["total_memory_writes"] = (int64_t)total_memory_writes;
    stats["total_memory_reads"] = (int64_t)total_memory_reads;
    stats["average_confidence"] = avg_confidence;
    stats["total_modules_loaded"] = (int64_t)modules.size();
    
    return stats;
}

void SVFInterruptAnalyzer::writeJsonToFile(const json::Object& root, const std::string& output_file) {
    std::error_code EC;
    raw_fd_ostream OS(output_file, EC);
    if (EC) {
        errs() << "❌ Error writing to " << output_file << ": " << EC.message() << "\n";
        return;
    }
    
    OS << formatv("{0:2}", json::Value(json::Object(root))) << "\n";
    outs() << "📄 Enhanced results written to: " << output_file << "\n";
}

//===----------------------------------------------------------------------===//
// 统计信息输出
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::printStatistics() const {
    outs() << "\n📈 Enhanced SVF Interrupt Analyzer Statistics\n";
    outs() << "=============================================\n";
    outs() << "Loaded modules: " << modules.size() << "\n";
    outs() << "Loaded bitcode files: " << loaded_bc_files.size() << "\n";
    outs() << "SVF initialized: " << (svf_initialized ? "Yes" : "No") << "\n";
    outs() << "Analysis features: Enhanced memory operations, data structures, function pointers\n";
    
#ifdef SVF_AVAILABLE
    if (svf_initialized && svfir) {
        outs() << "\nSVFIR Statistics:\n";
        outs() << "  Total nodes: " << svfir->getTotalNodeNum() << "\n";
        outs() << "  Total edges: " << svfir->getTotalEdgeNum() << "\n";
        outs() << "  Value nodes: " << svfir->getValueNodeNum() << "\n";
        
        SVF::LLVMModuleSet* moduleSet = SVF::LLVMModuleSet::getLLVMModuleSet();
        outs() << "  Object nodes: " << moduleSet->getObjNodeNum() << "\n";
    }
    
    if (pta) {
        outs() << "\nPointer Analysis Statistics:\n";
        outs() << "  Analysis completed successfully\n";
        outs() << "  Function pointer resolution enabled\n";
    }
    
    if (vfg) {
        outs() << "\nVFG Statistics:\n";
        outs() << "  VF nodes: " << vfg->getTotalNodeNum() << "\n";
        outs() << "  VF edges: " << vfg->getTotalEdgeNum() << "\n";
    }
#endif
    
    outs() << "\nEnhanced Analysis Cache:\n";
    outs() << "  Type name cache entries: " << type_name_cache.size() << "\n";
    outs() << "  Struct field cache entries: " << struct_field_cache.size() << "\n";
}

void SVFInterruptAnalyzer::printAnalysisBreakdown(const std::vector<InterruptHandlerResult>& results) const {
    outs() << "\n📊 Analysis Breakdown\n";
    outs() << "====================\n";
    
    // 按分析特征分组
    size_t handlers_with_writes = 0;
    size_t handlers_with_structures = 0;
    size_t handlers_with_pointers = 0;
    size_t high_confidence_handlers = 0;
    
    for (const auto& result : results) {
        if (result.memory_write_operations > 0) handlers_with_writes++;
        if (!result.data_structure_accesses.empty()) handlers_with_structures++;
        if (!result.function_pointer_targets.empty()) handlers_with_pointers++;
        if (result.confidence_score >= 80.0) high_confidence_handlers++;
    }
    
    outs() << "Handlers with memory writes: " << handlers_with_writes << "\n";
    outs() << "Handlers with data structure access: " << handlers_with_structures << "\n";
    outs() << "Handlers with function pointers: " << handlers_with_pointers << "\n";
    outs() << "High confidence handlers (≥80%): " << high_confidence_handlers << "\n";
    
    // 显示最复杂的处理函数
    if (!results.empty()) {
        auto max_it = std::max_element(results.begin(), results.end(),
                                      [](const InterruptHandlerResult& a, const InterruptHandlerResult& b) {
                                          return a.total_instructions < b.total_instructions;
                                      });
        
        if (max_it != results.end() && max_it->analysis_complete) {
            outs() << "\nMost complex handler: " << max_it->function_name 
                   << " (" << max_it->total_instructions << " instructions)\n";
        }
    }
}
