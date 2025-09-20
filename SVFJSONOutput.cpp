//===- SVFJSONOutput.cpp - SVF专用JSON输出实现 ---------------------------===//

#include "SVFJSONOutput.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <ctime>

using namespace llvm;

void SVFJSONOutputGenerator::outputResults(const std::vector<SVFInterruptHandlerAnalysis>& results,
                                          const std::string& output_file) {
    json::Value json_output = convertToJSON(results);
    
    std::error_code EC;
    raw_fd_ostream OS(output_file, EC);
    if (EC) {
        errs() << "Error writing to " << output_file << ": " << EC.message() << "\n";
        return;
    }
    
    OS << formatv("{0:2}", json_output) << "\n";
    outs() << "Results written to: " << output_file << "\n";
}

json::Value SVFJSONOutputGenerator::convertToJSON(const std::vector<SVFInterruptHandlerAnalysis>& results) {
    json::Object output;
    json::Array handlers_array;
    
    for (const auto& analysis : results) {
        handlers_array.push_back(convertHandlerAnalysis(analysis));
    }
    
    output["svf_interrupt_handlers"] = std::move(handlers_array);
    output["analysis_timestamp"] = (int64_t)std::time(nullptr);
    output["total_handlers"] = (int64_t)results.size();
    output["analyzer_type"] = "SVF";
    output["statistics"] = generateStatistics(results);
    
    return json::Value(std::move(output));
}

json::Object SVFJSONOutputGenerator::convertHandlerAnalysis(const SVFInterruptHandlerAnalysis& analysis) {
    json::Object handler;
    
    handler["function_name"] = analysis.function_name;
    handler["source_file"] = analysis.source_file;
    handler["svf_precision_score"] = analysis.svf_precision_score;
    handler["svf_analysis_complete"] = analysis.svf_analysis_complete;
    
    // Function pointer calls
    json::Array fp_calls;
    for (const auto& fp_result : analysis.function_pointer_calls) {
        fp_calls.push_back(convertFunctionPointerResult(fp_result));
    }
    handler["function_pointer_calls"] = std::move(fp_calls);
    
    // Struct usage
    json::Object struct_usage;
    for (const auto& struct_pair : analysis.struct_usage) {
        json::Array fields;
        for (const auto& field : struct_pair.second) {
            fields.push_back(convertStructFieldInfo(field));
        }
        struct_usage[struct_pair.first] = std::move(fields);
    }
    handler["struct_usage"] = std::move(struct_usage);
    
    // Access patterns
    json::Array patterns;
    for (const auto& pattern : analysis.access_patterns) {
        patterns.push_back(convertAccessPattern(pattern));
    }
    handler["access_patterns"] = std::move(patterns);
    
    // Pointed objects
    json::Array pointed_objects;
    for (Value* obj : analysis.pointed_objects) {
        if (obj && obj->hasName()) {
            pointed_objects.push_back(obj->getName().str());
        }
    }
    handler["pointed_objects"] = std::move(pointed_objects);
    
    return handler;
}

json::Object SVFJSONOutputGenerator::convertFunctionPointerResult(const SVFFunctionPointerResult& result) {
    json::Object fp_result;
    
    fp_result["source_function"] = result.source_function ? result.source_function->getName().str() : "unknown";
    fp_result["analysis_method"] = result.analysis_method;
    fp_result["is_precise"] = result.is_precise;
    
    json::Array targets;
    for (Function* target : result.possible_targets) {
        json::Object target_obj;
        target_obj["function_name"] = target->getName().str();
        
        auto conf_it = result.confidence_scores.find(target);
        target_obj["confidence"] = conf_it != result.confidence_scores.end() ? 
                                  (int64_t)conf_it->second : 50;
        
        targets.push_back(std::move(target_obj));
    }
    fp_result["possible_targets"] = std::move(targets);
    
    return fp_result;
}

json::Object SVFJSONOutputGenerator::convertStructFieldInfo(const SVFStructFieldInfo& field) {
    json::Object field_obj;
    
    field_obj["struct_name"] = field.struct_name;
    field_obj["field_name"] = field.field_name;
    field_obj["field_index"] = (int64_t)field.field_index;
    field_obj["is_function_pointer"] = field.is_function_pointer;
    
    if (field.is_function_pointer) {
        json::Array stored_funcs;
        for (Function* func : field.stored_functions) {
            stored_funcs.push_back(func->getName().str());
        }
        field_obj["stored_functions"] = std::move(stored_funcs);
    }
    
    return field_obj;
}

json::Object SVFJSONOutputGenerator::convertAccessPattern(const SVFMemoryAccessPattern& pattern) {
    json::Object pattern_obj;
    
    pattern_obj["pattern_name"] = pattern.pattern_name;
    pattern_obj["frequency"] = (int64_t)pattern.frequency;
    pattern_obj["is_device_access_pattern"] = pattern.is_device_access_pattern;
    pattern_obj["is_kernel_data_structure"] = pattern.is_kernel_data_structure;
    
    return pattern_obj;
}

json::Object SVFJSONOutputGenerator::generateStatistics(const std::vector<SVFInterruptHandlerAnalysis>& results) {
    json::Object stats;
    
    size_t total_fp_calls = 0;
    size_t total_structs = 0;
    size_t total_patterns = 0;
    double avg_precision = 0.0;
    size_t complete_analyses = 0;
    
    for (const auto& analysis : results) {
        total_fp_calls += analysis.function_pointer_calls.size();
        total_structs += analysis.struct_usage.size();
        total_patterns += analysis.access_patterns.size();
        avg_precision += analysis.svf_precision_score;
        if (analysis.svf_analysis_complete) {
            complete_analyses++;
        }
    }
    
    if (!results.empty()) {
        avg_precision /= results.size();
    }
    
    stats["total_function_pointer_calls"] = (int64_t)total_fp_calls;
    stats["total_struct_types"] = (int64_t)total_structs;
    stats["total_access_patterns"] = (int64_t)total_patterns;
    stats["average_precision_score"] = avg_precision;
    stats["complete_analyses"] = (int64_t)complete_analyses;
    stats["completion_rate"] = results.empty() ? 0.0 : 
                              (double)complete_analyses / results.size() * 100.0;
    
    return stats;
}

// Report Generator Implementation
void SVFReportGenerator::generateMarkdownReport(const std::vector<SVFInterruptHandlerAnalysis>& results,
                                               const std::string& output_file) {
    std::ofstream file(output_file);
    if (!file.is_open()) {
        errs() << "Failed to create report: " << output_file << "\n";
        return;
    }
    
    file << "# SVF Interrupt Handler Analysis Report\n\n";
    file << "Generated on: " << std::ctime(nullptr) << "\n";
    file << "Total handlers analyzed: " << results.size() << "\n\n";
    
    // Summary statistics
    size_t total_fp_calls = 0;
    size_t total_structs = 0;
    double avg_precision = 0.0;
    
    for (const auto& analysis : results) {
        total_fp_calls += analysis.function_pointer_calls.size();
        total_structs += analysis.struct_usage.size();
        avg_precision += analysis.svf_precision_score;
    }
    
    if (!results.empty()) {
        avg_precision /= results.size();
    }
    
    file << "## Summary\n\n";
    file << "- Function pointer calls: " << total_fp_calls << "\n";
    file << "- Struct types analyzed: " << total_structs << "\n";
    file << "- Average precision: " << std::fixed << std::setprecision(1) << avg_precision << "\n\n";
    
    // Individual handler details
    file << "## Handler Details\n\n";
    for (const auto& analysis : results) {
        file << "### " << analysis.function_name << "\n\n";
        file << "- Source: " << analysis.source_file << "\n";
        file << "- Precision score: " << analysis.svf_precision_score << "\n";
        file << "- Function pointer calls: " << analysis.function_pointer_calls.size() << "\n";
        file << "- Struct types: " << analysis.struct_usage.size() << "\n";
        file << "- Access patterns: " << analysis.access_patterns.size() << "\n\n";
    }
    
    file.close();
    outs() << "Report generated: " << output_file << "\n";
}

void SVFReportGenerator::generateFunctionPointerSummary(const std::vector<SVFInterruptHandlerAnalysis>& results,
                                                       const std::string& output_file) {
    std::ofstream file(output_file);
    if (!file.is_open()) return;
    
    file << "# Function Pointer Analysis Summary\n\n";
    
    for (const auto& analysis : results) {
        if (!analysis.function_pointer_calls.empty()) {
            file << "## " << analysis.function_name << "\n\n";
            
            for (const auto& fp_call : analysis.function_pointer_calls) {
                file << "### " << fp_call.analysis_method << " analysis\n";
                file << "- Precision: " << (fp_call.is_precise ? "High" : "Standard") << "\n";
                file << "- Targets found: " << fp_call.possible_targets.size() << "\n\n";
                
                for (Function* target : fp_call.possible_targets) {
                    auto conf_it = fp_call.confidence_scores.find(target);
                    int confidence = conf_it != fp_call.confidence_scores.end() ? conf_it->second : 50;
                    file << "  - " << target->getName().str() << " (confidence: " << confidence << ")\n";
                }
                file << "\n";
            }
        }
    }
    
    file.close();
}

void SVFReportGenerator::generateStructUsageReport(const std::vector<SVFInterruptHandlerAnalysis>& results,
                                                  const std::string& output_file) {
    std::ofstream file(output_file);
    if (!file.is_open()) return;
    
    file << "# Struct Usage Analysis\n\n";
    
    std::map<std::string, int> struct_usage_count;
    
    for (const auto& analysis : results) {
        for (const auto& struct_pair : analysis.struct_usage) {
            struct_usage_count[struct_pair.first]++;
        }
    }
    
    file << "## Most Used Structs\n\n";
    for (const auto& count_pair : struct_usage_count) {
        file << "- " << count_pair.first << ": used in " << count_pair.second << " handlers\n";
    }
    
    file.close();
}
