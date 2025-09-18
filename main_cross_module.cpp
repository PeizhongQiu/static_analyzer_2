//===- main_cross_module.cpp - 跨模块中断处理函数分析器主程序 --------------===//
//
// 专注于跨bc文件分析的主程序，提供完整的中断处理函数分析能力
// 
// 功能特性：
// • 跨模块函数指针解析
// • static/global 符号区分  
// • 数据流分析
// • 深度函数分析
//
//===----------------------------------------------------------------------===//

#include "CrossModuleAnalyzer.h"
#include "CompileCommandsParser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include <string>
#include <vector>
#include <chrono>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 程序配置和参数
//===----------------------------------------------------------------------===//

struct AnalysisConfig {
    std::string compile_commands_path;
    std::string handler_json_path;
    std::string output_path = "cross_module_analysis_results.json";
    size_t max_modules = 0;        // 0 = 无限制
    bool verbose = false;
    bool show_stats = false;
    bool show_help = false;
    bool enable_deep_analysis = true;
    bool enable_dataflow = true;
    
    void print() const {
        outs() << "=== Cross-Module IRQ Analysis Configuration ===\n";
        outs() << "Input files:\n";
        outs() << "  Compile commands: " << compile_commands_path << "\n";
        outs() << "  Handler definitions: " << handler_json_path << "\n";
        outs() << "Output:\n";
        outs() << "  Results file: " << output_path << "\n";
        outs() << "Analysis options:\n";
        outs() << "  Max modules: " << (max_modules ? std::to_string(max_modules) : "unlimited") << "\n";
        outs() << "  Deep function analysis: " << (enable_deep_analysis ? "enabled" : "disabled") << "\n";
        outs() << "  Data-flow analysis: " << (enable_dataflow ? "enabled" : "disabled") << "\n";
        outs() << "  Verbose output: " << (verbose ? "enabled" : "disabled") << "\n";
        outs() << "\n";
    }
};

//===----------------------------------------------------------------------===//
// 命令行参数解析
//===----------------------------------------------------------------------===//

void printUsage(const char* program_name) {
    outs() << "Cross-Module Interrupt Handler Analyzer\n";
    outs() << "========================================\n";
    outs() << "Advanced static analysis tool for kernel interrupt handlers with cross-module support.\n\n";
    
    outs() << "Usage: " << program_name << " [options]\n\n";
    
    outs() << "Required Options:\n";
    outs() << "  --compile-commands=<file>  Path to compile_commands.json file\n";
    outs() << "  --handlers=<file>          Path to handler.json configuration file\n\n";
    
    outs() << "Optional Options:\n";
    outs() << "  --output=<file>            Output JSON file (default: cross_module_analysis_results.json)\n";
    outs() << "  --max-modules=<number>     Maximum modules to load (default: unlimited)\n";
    outs() << "  --no-deep-analysis         Disable deep function pointer analysis\n";
    outs() << "  --no-dataflow             Disable data-flow analysis\n";
    outs() << "  --verbose                  Enable detailed output\n";
    outs() << "  --stats                    Show comprehensive statistics\n";
    outs() << "  --help, -h                 Show this help message\n\n";
    
    outs() << "Advanced Features:\n";
    outs() << "  ✓ Cross-module function pointer resolution\n";
    outs() << "  ✓ Static vs Global symbol distinction\n";
    outs() << "  ✓ Recursive data-flow analysis\n";
    outs() << "  ✓ Deep indirect call analysis\n";
    outs() << "  ✓ Comprehensive memory access tracking\n\n";
    
    outs() << "Examples:\n";
    outs() << "  Basic analysis:\n";
    outs() << "    " << program_name << " --compile-commands=compile_commands.json \\\n";
    outs() << "                          --handlers=handler.json\n\n";
    
    outs() << "  Memory-constrained analysis:\n";
    outs() << "    " << program_name << " --compile-commands=compile_commands.json \\\n";
    outs() << "                          --handlers=handler.json \\\n";
    outs() << "                          --max-modules=50 --verbose\n\n";
    
    outs() << "  Full analysis with statistics:\n";
    outs() << "    " << program_name << " --compile-commands=compile_commands.json \\\n";
    outs() << "                          --handlers=handler.json \\\n";
    outs() << "                          --output=detailed_results.json \\\n";
    outs() << "                          --verbose --stats\n\n";
}

bool parseCommandLineArgs(int argc, char** argv, AnalysisConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            config.show_help = true;
            return true;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--stats") {
            config.show_stats = true;
        } else if (arg == "--no-deep-analysis") {
            config.enable_deep_analysis = false;
        } else if (arg == "--no-dataflow") {
            config.enable_dataflow = false;
        } else if (arg.find("--compile-commands=") == 0) {
            config.compile_commands_path = arg.substr(19);
        } else if (arg.find("--handlers=") == 0) {
            config.handler_json_path = arg.substr(11);
        } else if (arg.find("--output=") == 0) {
            config.output_path = arg.substr(9);
        } else if (arg.find("--max-modules=") == 0) {
            std::string value = arg.substr(14);
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
                errs() << "Error: Invalid max-modules value: " << value << "\n";
                return false;
            }
            config.max_modules = std::stoull(value);
        } else {
            errs() << "Error: Unknown argument: " << arg << "\n";
            errs() << "Use --help for usage information.\n";
            return false;
        }
    }
    
    // 验证必需参数
    if (!config.show_help) {
        if (config.compile_commands_path.empty()) {
            errs() << "Error: Missing required argument --compile-commands\n";
            return false;
        }
        if (config.handler_json_path.empty()) {
            errs() << "Error: Missing required argument --handlers\n";
            return false;
        }
    }
    
    return true;
}

//===----------------------------------------------------------------------===//
// 输入验证和预处理
//===----------------------------------------------------------------------===//

struct InputValidationResult {
    bool success = false;
    std::vector<std::string> existing_bc_files;
    size_t total_compile_commands = 0;
    size_t missing_files = 0;
    std::string error_message;
};

InputValidationResult validateAndPrepareInputs(const AnalysisConfig& config) {
    InputValidationResult result;
    
    // 1. 验证输入文件存在性
    if (!sys::fs::exists(config.compile_commands_path)) {
        result.error_message = "Compile commands file not found: " + config.compile_commands_path;
        return result;
    }
    
    if (!sys::fs::exists(config.handler_json_path)) {
        result.error_message = "Handler JSON file not found: " + config.handler_json_path;
        return result;
    }
    
    // 2. 解析compile_commands.json
    CompileCommandsParser parser;
    if (!parser.parseFromFile(config.compile_commands_path)) {
        result.error_message = "Failed to parse compile_commands.json";
        return result;
    }
    
    result.total_compile_commands = parser.getCommandCount();
    if (result.total_compile_commands == 0) {
        result.error_message = "No compile commands found in " + config.compile_commands_path;
        return result;
    }
    
    // 3. 验证bitcode文件存在性
    std::vector<std::string> all_bc_files = parser.getBitcodeFiles();
    if (all_bc_files.empty()) {
        result.error_message = "No bitcode files (.bc) found. Please compile with -emit-llvm first.";
        return result;
    }
    
    // 4. 筛选存在的文件
    for (const auto& bc_file : all_bc_files) {
        if (sys::fs::exists(bc_file)) {
            result.existing_bc_files.push_back(bc_file);
        } else {
            result.missing_files++;
        }
    }
    
    if (result.existing_bc_files.empty()) {
        result.error_message = "No bitcode files exist on disk. All " + 
                              std::to_string(all_bc_files.size()) + " files are missing.";
        return result;
    }
    
    // 5. 应用max_modules限制
    if (config.max_modules > 0 && result.existing_bc_files.size() > config.max_modules) {
        result.existing_bc_files.resize(config.max_modules);
        if (config.verbose) {
            outs() << "Note: Limited to first " << config.max_modules 
                   << " modules (out of " << (result.existing_bc_files.size() + result.missing_files) 
                   << " available)\n";
        }
    }
    
    result.success = true;
    return result;
}

//===----------------------------------------------------------------------===//
// 分析统计和报告
//===----------------------------------------------------------------------===//

struct AnalysisStatistics {
    // 时间统计
    std::chrono::milliseconds module_load_time{0};
    std::chrono::milliseconds symbol_build_time{0};
    std::chrono::milliseconds analysis_time{0};
    std::chrono::milliseconds total_time{0};
    
    // 模块统计
    size_t total_modules_attempted = 0;
    size_t modules_loaded_successfully = 0;
    size_t modules_failed_to_load = 0;
    
    // 符号统计
    size_t global_functions = 0;
    size_t static_functions = 0;
    size_t global_variables = 0;
    size_t static_variables = 0;
    size_t struct_types = 0;
    
    // 分析结果统计
    size_t handlers_found = 0;
    size_t total_memory_accesses = 0;
    size_t high_confidence_accesses = 0;
    size_t device_related_accesses = 0;
    size_t cross_module_calls = 0;
    size_t resolved_function_pointers = 0;
    size_t indirect_calls_analyzed = 0;
    
    void print(bool verbose = false) const {
        outs() << "\n=== Cross-Module Analysis Statistics ===\n";
        
        // 基本结果
        outs() << "Analysis Results:\n";
        outs() << "  Interrupt handlers found: " << handlers_found << "\n";
        outs() << "  Total memory accesses: " << total_memory_accesses << "\n";
        outs() << "  High confidence accesses: " << high_confidence_accesses 
               << " (" << (total_memory_accesses > 0 ? (high_confidence_accesses * 100 / total_memory_accesses) : 0) << "%)\n";
        outs() << "  Device-related accesses: " << device_related_accesses << "\n";
        outs() << "  Cross-module function calls: " << cross_module_calls << "\n";
        outs() << "  Resolved function pointers: " << resolved_function_pointers << "\n";
        outs() << "  Indirect calls analyzed: " << indirect_calls_analyzed << "\n";
        
        if (verbose) {
            // 详细统计
            outs() << "\nModule Loading:\n";
            outs() << "  Modules attempted: " << total_modules_attempted << "\n";
            outs() << "  Successfully loaded: " << modules_loaded_successfully << "\n";
            outs() << "  Failed to load: " << modules_failed_to_load << "\n";
            
            outs() << "\nSymbol Table:\n";
            outs() << "  Global functions: " << global_functions << "\n";
            outs() << "  Static functions: " << static_functions << "\n";
            outs() << "  Global variables: " << global_variables << "\n";
            outs() << "  Static variables: " << static_variables << "\n";
            outs() << "  Structure types: " << struct_types << "\n";
            
            // 性能统计
            outs() << "\nTiming Analysis:\n";
            outs() << "  Module loading: " << module_load_time.count() << " ms\n";
            outs() << "  Symbol table building: " << symbol_build_time.count() << " ms\n";
            outs() << "  Handler analysis: " << analysis_time.count() << " ms\n";
            outs() << "  Total time: " << total_time.count() << " ms\n";
            
            // 效率指标
            if (total_time.count() > 0) {
                outs() << "\nEfficiency Metrics:\n";
                outs() << "  Modules per second: " << (modules_loaded_successfully * 1000.0 / total_time.count()) << "\n";
                outs() << "  Handlers per second: " << (handlers_found * 1000.0 / total_time.count()) << "\n";
                if (handlers_found > 0) {
                    outs() << "  Avg time per handler: " << (analysis_time.count() / handlers_found) << " ms\n";
                }
            }
        }
    }
};

void collectStatisticsFromAnalyzer(const CrossModuleAnalyzer& analyzer, 
                                  const std::vector<InterruptHandlerAnalysis>& results,
                                  AnalysisStatistics& stats) {
    // 符号表统计
    stats.global_functions = analyzer.getTotalFunctions();
    stats.static_functions = analyzer.getTotalStaticFunctions();
    stats.global_variables = analyzer.getTotalGlobalVars();
    stats.static_variables = analyzer.getTotalStaticVars();
    stats.struct_types = analyzer.getTotalStructTypes();
    
    // 分析结果统计
    stats.handlers_found = results.size();
    
    for (const auto& analysis : results) {
        stats.total_memory_accesses += analysis.total_memory_accesses.size();
        
        for (const auto& access : analysis.total_memory_accesses) {
            if (access.confidence >= 80) {
                stats.high_confidence_accesses++;
            }
            if (access.isDeviceRelatedAccess()) {
                stats.device_related_accesses++;
            }
        }
        
        for (const auto& call : analysis.function_calls) {
            if (call.analysis_reason.find("cross_module") != std::string::npos) {
                stats.cross_module_calls++;
            }
        }
        
        stats.indirect_calls_analyzed += analysis.indirect_call_analyses.size();
        
        for (const auto& indirect : analysis.indirect_call_analyses) {
            stats.resolved_function_pointers += indirect.fp_analysis.possible_targets.size();
        }
    }
}

//===----------------------------------------------------------------------===//
// 主程序逻辑
//===----------------------------------------------------------------------===//

int main(int argc, char** argv) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 解析命令行参数
    AnalysisConfig config;
    if (!parseCommandLineArgs(argc, argv, config)) {
        return 1;
    }
    
    if (config.show_help) {
        printUsage(argv[0]);
        return 0;
    }
    
    // 显示配置信息
    if (config.verbose) {
        config.print();
    } else {
        outs() << "Cross-Module IRQ Analysis Tool\n";
        outs() << "Input: " << config.compile_commands_path << " + " << config.handler_json_path << "\n";
        outs() << "Output: " << config.output_path << "\n\n";
    }
    
    // 输入验证和预处理
    auto validation_start = std::chrono::high_resolution_clock::now();
    InputValidationResult validation = validateAndPrepareInputs(config);
    if (!validation.success) {
        errs() << "Input validation failed: " << validation.error_message << "\n";
        return 1;
    }
    
    if (config.verbose) {
        outs() << "Input Validation Results:\n";
        outs() << "  Total compile commands: " << validation.total_compile_commands << "\n";
        outs() << "  Bitcode files found: " << validation.existing_bc_files.size() << "\n";
        outs() << "  Missing files: " << validation.missing_files << "\n";
        if (validation.missing_files > 0) {
            outs() << "  (Missing files will be skipped)\n";
        }
        outs() << "\n";
    }
    
    // 初始化统计信息
    AnalysisStatistics stats;
    stats.total_modules_attempted = validation.existing_bc_files.size();
    
    // 创建跨模块分析器
    LLVMContext Context;
    CrossModuleAnalyzer analyzer;
    
    // 加载所有模块
    auto load_start = std::chrono::high_resolution_clock::now();
    if (!analyzer.loadAllModules(validation.existing_bc_files, Context)) {
        errs() << "Failed to load modules for cross-module analysis\n";
        return 1;
    }
    auto load_end = std::chrono::high_resolution_clock::now();
    stats.module_load_time = std::chrono::duration_cast<std::chrono::milliseconds>(load_end - load_start);
    stats.modules_loaded_successfully = analyzer.getModuleCount();
    stats.modules_failed_to_load = stats.total_modules_attempted - stats.modules_loaded_successfully;
    
    // 进行跨模块分析
    auto analysis_start = std::chrono::high_resolution_clock::now();
    std::vector<InterruptHandlerAnalysis> results = 
        analyzer.analyzeAllHandlers(config.handler_json_path);
    auto analysis_end = std::chrono::high_resolution_clock::now();
    stats.analysis_time = std::chrono::duration_cast<std::chrono::milliseconds>(analysis_end - analysis_start);
    
    if (results.empty()) {
        outs() << "\n⚠️  No interrupt handlers found!\n\n";
        outs() << "Possible causes:\n";
        outs() << "1. Handler names in " << config.handler_json_path << " don't match functions in bitcode\n";
        outs() << "2. Handler functions were optimized away during compilation\n";
        outs() << "3. Bitcode files don't contain the expected modules\n\n";
        outs() << "Suggestions:\n";
        outs() << "• Check handler.json for correct function names\n";
        outs() << "• Verify bitcode compilation with: llvm-nm file.bc | grep handler_name\n";
        outs() << "• Try with --verbose to see detailed loading information\n";
        return 1;
    }
    
    // 收集和计算统计信息
    collectStatisticsFromAnalyzer(analyzer, results, stats);
    
    // 输出结果
    JSONOutputGenerator json_generator;
    json_generator.outputAnalysisResults(results, config.output_path);
    
    // 计算总时间
    auto end_time = std::chrono::high_resolution_clock::now();
    stats.total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // 显示最终结果摘要
    outs() << "\n=== Analysis Completed Successfully ===\n";
    outs() << "📊 Results Summary:\n";
    outs() << "  Interrupt handlers analyzed: " << results.size() << "\n";
    outs() << "  Total memory accesses found: " << stats.total_memory_accesses << "\n";
    outs() << "  Cross-module function calls: " << stats.cross_module_calls << "\n";
    outs() << "  Function pointers resolved: " << stats.resolved_function_pointers << "\n";
    outs() << "\n📁 Output written to: " << config.output_path << "\n";
    outs() << "⏱️  Total analysis time: " << stats.total_time.count() << " ms\n";
    
    // 显示详细统计（如果请求）
    if (config.show_stats) {
        stats.print(true);
    }
    
    // 性能和质量指标
    outs() << "\n🎯 Quality Metrics:\n";
    if (stats.total_memory_accesses > 0) {
        double confidence_ratio = (double)stats.high_confidence_accesses / stats.total_memory_accesses * 100.0;
        outs() << "  High confidence access rate: " << confidence_ratio << "%\n";
    }
    
    if (stats.cross_module_calls > 0) {
        outs() << "  Cross-module analysis effectiveness: HIGH\n";
    } else {
        outs() << "  Cross-module analysis effectiveness: LOW (consider including more modules)\n";
    }
    
    outs() << "\n✅ Cross-module analysis completed successfully!\n";
    
    return 0;
}
