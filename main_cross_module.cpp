//===- main_cross_module.cpp - 完整的跨模块中断处理函数分析器主程序 --------===//

#include "CrossModuleAnalyzer.h"
#include "CompileCommandsParser.h"
#include "FilteringEngine.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include <string>
#include <vector>
#include <chrono>
#include <csignal>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 错误处理和信号处理
//===----------------------------------------------------------------------===//

volatile sig_atomic_t signal_received = 0;

void signalHandler(int sig) {
    signal_received = sig;
    errs() << "\n💥 收到信号 " << sig;
    switch(sig) {
        case SIGSEGV: errs() << " (SIGSEGV - 段错误)"; break;
        case SIGABRT: errs() << " (SIGABRT - 异常终止)"; break;
        case SIGFPE:  errs() << " (SIGFPE - 浮点异常)"; break;
        case SIGILL:  errs() << " (SIGILL - 非法指令)"; break;
        default: errs() << " (未知信号)"; break;
    }
    errs() << "\n程序异常终止。建议使用 gdb 进行调试。\n";
    errs() << "调试命令: gdb ./irq_analyzer_cross_module\n";
    std::_Exit(128 + sig);
}

void setupSignalHandlers() {
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGFPE, signalHandler);
    std::signal(SIGILL, signalHandler);
}

//===----------------------------------------------------------------------===//
// 程序配置和参数
//===----------------------------------------------------------------------===//

struct AnalysisConfig {
    std::string compile_commands_path;
    std::string handler_json_path;
    std::string output_path;
    std::string filtering_level;        // 过滤级别
    size_t max_modules;
    bool verbose;
    bool show_stats;
    bool show_help;
    bool enable_deep_analysis;
    bool enable_dataflow;
    bool enable_svf_analysis;           // SVF分析选项
    bool show_filtering_stats;          // 显示过滤统计
    
    AnalysisConfig() : 
        output_path("cross_module_analysis_results.json"),
        filtering_level("moderate"),    // 默认适度过滤
        max_modules(0),
        verbose(false),
        show_stats(false), 
        show_help(false),
        enable_deep_analysis(true),
        enable_dataflow(true),
        enable_svf_analysis(false),     // 默认关闭SVF
        show_filtering_stats(false) {}
    
    void print() const {
        outs() << "=== Cross-Module IRQ Analysis Configuration ===\n";
        outs() << "Input files:\n";
        outs() << "  Compile commands: " << compile_commands_path << "\n";
        outs() << "  Handler definitions: " << handler_json_path << "\n";
        outs() << "Output:\n";
        outs() << "  Results file: " << output_path << "\n";
        outs() << "Analysis options:\n";
        outs() << "  Max modules: " << (max_modules ? std::to_string(max_modules) : "unlimited") << "\n";
        outs() << "  Filtering level: " << filtering_level << "\n";
        outs() << "  Deep function analysis: " << (enable_deep_analysis ? "enabled" : "disabled") << "\n";
        outs() << "  Data-flow analysis: " << (enable_dataflow ? "enabled" : "disabled") << "\n";
        outs() << "  SVF analysis: " << (enable_svf_analysis ? "enabled" : "disabled") << "\n";
        outs() << "  Verbose output: " << (verbose ? "enabled" : "disabled") << "\n";
        outs() << "\n";
    }
};

//===----------------------------------------------------------------------===//
// 命令行参数解析
//===----------------------------------------------------------------------===//

void printUsage(const char* program_name) {
    outs() << "Cross-Module Interrupt Handler Analyzer with SVF Support\n";
    outs() << "========================================================\n";
    outs() << "Advanced static analysis tool for kernel interrupt handlers.\n\n";
    
    outs() << "Usage: " << program_name << " [options]\n\n";
    
    outs() << "Required Options:\n";
    outs() << "  --compile-commands=<file>  Path to compile_commands.json file\n";
    outs() << "  --handlers=<file>          Path to handler.json configuration file\n\n";
    
    outs() << "Optional Options:\n";
    outs() << "  --output=<file>            Output JSON file (default: cross_module_analysis_results.json)\n";
    outs() << "  --max-modules=<number>     Maximum modules to load (default: unlimited)\n";
    outs() << "  --filter=<level>           Filtering level (default: moderate)\n";
    outs() << "  --no-deep-analysis         Disable deep function pointer analysis\n";
    outs() << "  --no-dataflow             Disable data-flow analysis\n";
    outs() << "  --svf-analysis            Enable SVF-enhanced pointer analysis\n";
    outs() << "  --verbose                  Enable detailed output\n";
    outs() << "  --stats                    Show comprehensive statistics\n";
    outs() << "  --filter-stats            Show filtering statistics\n";
    outs() << "  --help, -h                 Show this help message\n\n";
    
    outs() << "Filtering Levels:\n";
    FilteringConfigs::printConfigHelp();
    outs() << "\n";
    
    outs() << "Examples:\n";
    outs() << "  Basic analysis with moderate filtering (recommended):\n";
    outs() << "    " << program_name << " --compile-commands=compile_commands.json \\\n";
    outs() << "                          --handlers=handler.json\n\n";
    
    outs() << "  Strict filtering for fuzzing target identification:\n";
    outs() << "    " << program_name << " --compile-commands=compile_commands.json \\\n";
    outs() << "                          --handlers=handler.json \\\n";
    outs() << "                          --filter=strict --filter-stats\n\n";
    
    outs() << "  SVF-enhanced analysis:\n";
    outs() << "    " << program_name << " --compile-commands=compile_commands.json \\\n";
    outs() << "                          --handlers=handler.json \\\n";
    outs() << "                          --svf-analysis --verbose\n\n";
    
    outs() << "  No filtering (complete analysis):\n";
    outs() << "    " << program_name << " --compile-commands=compile_commands.json \\\n";
    outs() << "                          --handlers=handler.json \\\n";
    outs() << "                          --filter=none\n\n";
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
        } else if (arg == "--filter-stats") {
            config.show_filtering_stats = true;
        } else if (arg == "--no-deep-analysis") {
            config.enable_deep_analysis = false;
        } else if (arg == "--no-dataflow") {
            config.enable_dataflow = false;
        } else if (arg == "--svf-analysis") {
            config.enable_svf_analysis = true;
        } else if (arg.find("--compile-commands=") == 0) {
            config.compile_commands_path = arg.substr(19);
        } else if (arg.find("--handlers=") == 0) {
            config.handler_json_path = arg.substr(11);
        } else if (arg.find("--output=") == 0) {
            config.output_path = arg.substr(9);
        } else if (arg.find("--filter=") == 0) {
            config.filtering_level = arg.substr(9);
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
// 安全的输入验证
//===----------------------------------------------------------------------===//

struct InputValidationResult {
    bool success;
    std::vector<std::string> existing_bc_files;
    size_t total_compile_commands;
    size_t missing_files;
    std::string error_message;
    
    InputValidationResult() : success(false), total_compile_commands(0), missing_files(0) {}
};

InputValidationResult validateAndPrepareInputs(const AnalysisConfig& config) {
    InputValidationResult result;
    
    // 验证输入文件存在性
    if (!sys::fs::exists(config.compile_commands_path)) {
        result.error_message = "Compile commands file not found: " + config.compile_commands_path;
        return result;
    }
    
    if (!sys::fs::exists(config.handler_json_path)) {
        result.error_message = "Handler JSON file not found: " + config.handler_json_path;
        return result;
    }
    
    // 解析compile_commands.json
    CompileCommandsParser parser;
    if (!parser.parseFromFile(config.compile_commands_path)) {
        result.error_message = "Failed to parse compile_commands.json";
        return result;
    }
    
    result.total_compile_commands = parser.getCommandCount();
    if (result.total_compile_commands == 0) {
        result.error_message = "No compile commands found";
        return result;
    }
    
    // 验证bitcode文件
    std::vector<std::string> all_bc_files = parser.getBitcodeFiles();
    if (all_bc_files.empty()) {
        result.error_message = "No bitcode files (.bc) found. Please compile with -emit-llvm first.";
        return result;
    }
    
    // 筛选存在的文件
    for (const auto& bc_file : all_bc_files) {
        if (sys::fs::exists(bc_file)) {
            result.existing_bc_files.push_back(bc_file);
        } else {
            result.missing_files++;
        }
    }
    
    if (result.existing_bc_files.empty()) {
        result.error_message = "No bitcode files exist on disk";
        return result;
    }
    
    // 应用限制
    if (config.max_modules > 0 && result.existing_bc_files.size() > config.max_modules) {
        result.existing_bc_files.resize(config.max_modules);
    }
    
    result.success = true;
    return result;
}

//===----------------------------------------------------------------------===//
// 统计信息
//===----------------------------------------------------------------------===//

struct AnalysisStatistics {
    std::chrono::milliseconds total_time;
    size_t handlers_found;
    size_t total_memory_accesses;
    size_t high_confidence_accesses;
    size_t cross_module_calls;
    size_t modules_loaded;
    size_t svf_enhanced_accesses;
    size_t filtered_accesses;
    
    AnalysisStatistics() : total_time(0), handlers_found(0), total_memory_accesses(0), 
                          high_confidence_accesses(0), cross_module_calls(0), modules_loaded(0),
                          svf_enhanced_accesses(0), filtered_accesses(0) {}
    
    void print() const {
        outs() << "\n=== Analysis Statistics ===\n";
        outs() << "  Handlers found: " << handlers_found << "\n";
        outs() << "  Modules loaded: " << modules_loaded << "\n";
        outs() << "  Total memory accesses: " << total_memory_accesses << "\n";
        outs() << "  High confidence accesses: " << high_confidence_accesses << "\n";
        outs() << "  Cross-module calls: " << cross_module_calls << "\n";
        if (svf_enhanced_accesses > 0) {
            outs() << "  SVF enhanced accesses: " << svf_enhanced_accesses << "\n";
        }
        if (filtered_accesses > 0) {
            outs() << "  Filtered accesses: " << filtered_accesses << "\n";
        }
        outs() << "  Total time: " << total_time.count() << " ms\n";
    }
};

void collectStatistics(const CrossModuleAnalyzer& analyzer, 
                      const std::vector<InterruptHandlerAnalysis>& results,
                      AnalysisStatistics& stats) {
    stats.handlers_found = results.size();
    stats.modules_loaded = analyzer.getModuleCount();
    
    for (const auto& analysis : results) {
        stats.total_memory_accesses += analysis.total_memory_accesses.size();
        
        for (const auto& access : analysis.total_memory_accesses) {
            if (access.confidence >= 80) {
                stats.high_confidence_accesses++;
            }
            if (access.chain_description.find("SVF_enhanced") != std::string::npos) {
                stats.svf_enhanced_accesses++;
            }
        }
        
        for (const auto& call : analysis.function_calls) {
            if (call.analysis_reason.find("cross_module") != std::string::npos) {
                stats.cross_module_calls++;
            }
        }
    }
}

//===----------------------------------------------------------------------===//
// 主程序逻辑
//===----------------------------------------------------------------------===//

int main(int argc, char** argv) {
    setupSignalHandlers();
    
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
    
    // 验证过滤级别
    auto available_configs = FilteringConfigs::getAvailableConfigNames();
    if (std::find(available_configs.begin(), available_configs.end(), config.filtering_level) == available_configs.end()) {
        errs() << "Error: Invalid filtering level: " << config.filtering_level << "\n";
        errs() << "Available levels: ";
        for (size_t i = 0; i < available_configs.size(); ++i) {
            if (i > 0) errs() << ", ";
            errs() << available_configs[i];
        }
        errs() << "\n";
        return 1;
    }
    
    // 显示配置信息
    if (config.verbose) {
        config.print();
    } else {
        outs() << "Cross-Module IRQ Analysis Tool with SVF Support\n";
        outs() << "Input: " << config.compile_commands_path << " + " << config.handler_json_path << "\n";
        outs() << "Output: " << config.output_path << "\n";
        outs() << "Filtering: " << config.filtering_level << "\n";
        if (config.enable_svf_analysis) {
            outs() << "SVF Analysis: Enabled\n";
        }
        outs() << "\n";
    }
    
    // 输入验证
    InputValidationResult validation = validateAndPrepareInputs(config);
    if (!validation.success) {
        errs() << "Input validation failed: " << validation.error_message << "\n";
        return 1;
    }
    
    if (config.verbose) {
        outs() << "Input Validation Results:\n";
        outs() << "  Total compile commands: " << validation.total_compile_commands << "\n";
        outs() << "  Bitcode files found: " << validation.existing_bc_files.size() << "\n";
        outs() << "  Missing files: " << validation.missing_files << "\n\n";
    }
    
    // 创建分析器 - 使用堆分配避免栈上的大对象
    LLVMContext* Context = new LLVMContext();
    if (!Context) {
        errs() << "Failed to create LLVM context\n";
        return 1;
    }
    
    CrossModuleAnalyzer* analyzer = new CrossModuleAnalyzer();
    if (!analyzer) {
        errs() << "Failed to create analyzer\n";
        delete Context;
        return 1;
    }
    
    // 配置SVF分析
    if (config.enable_svf_analysis) {
        outs() << "SVF analysis enabled\n";
        analyzer->enableSVFAnalysis(true);
    }
    
    // 配置过滤引擎
    FilteringConfig filter_config = FilteringConfigs::getConfigByName(config.filtering_level);
    analyzer->setFilteringConfig(filter_config);
    
    if (config.verbose) {
        outs() << "Filtering configuration: " << config.filtering_level << "\n";
        outs() << "  Min confidence threshold: " << filter_config.min_confidence_threshold << "\n";
        outs() << "  Include constant addresses: " << (filter_config.include_constant_addresses ? "yes" : "no") << "\n";
        outs() << "  Include dev_id chains: " << (filter_config.include_dev_id_chains ? "yes" : "no") << "\n\n";
    }
    
    // 加载模块
    outs() << "Loading modules...\n";
    if (!analyzer->loadAllModules(validation.existing_bc_files, *Context)) {
        errs() << "Failed to load modules\n";
        delete analyzer;
        delete Context;
        return 1;
    }
    
    // 进行分析
    outs() << "Performing cross-module analysis";
    if (config.enable_svf_analysis) {
        outs() << " with SVF enhancement";
    }
    outs() << "...\n";
    
    std::vector<InterruptHandlerAnalysis> results = analyzer->analyzeAllHandlers(config.handler_json_path);
    
    if (results.empty()) {
        outs() << "\n⚠️  No interrupt handlers found!\n";
        delete analyzer;
        delete Context;
        return 1;
    }
    
    // 输出结果
    JSONOutputGenerator json_generator;
    json_generator.outputAnalysisResults(results, config.output_path);
    
    // 收集统计信息
    AnalysisStatistics stats;
    auto end_time = std::chrono::high_resolution_clock::now();
    stats.total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    collectStatistics(*analyzer, results, stats);
    
    // 收集过滤统计
    if (analyzer->getFilteringEngine()) {
        const auto& filter_stats = analyzer->getFilteringEngine()->getStats();
        stats.filtered_accesses = filter_stats.total_accesses - filter_stats.remaining_accesses;
    }
    
    // 显示结果
    outs() << "\n=== Analysis Completed Successfully ===\n";
    outs() << "📊 Results Summary:\n";
    outs() << "  Interrupt handlers analyzed: " << results.size() << "\n";
    outs() << "  Total memory accesses: " << stats.total_memory_accesses << "\n";
    outs() << "  Cross-module function calls: " << stats.cross_module_calls << "\n";
    
    if (config.enable_svf_analysis && stats.svf_enhanced_accesses > 0) {
        outs() << "  SVF enhanced accesses: " << stats.svf_enhanced_accesses << "\n";
    }
    
    if (stats.filtered_accesses > 0) {
        outs() << "  Filtered accesses: " << stats.filtered_accesses << "\n";
    }
    
    outs() << "📁 Output written to: " << config.output_path << "\n";
    outs() << "⏱️  Total time: " << stats.total_time.count() << " ms\n";
    
    if (config.show_stats) {
        stats.print();
    }
    
    // 显示过滤统计
    if (config.show_filtering_stats && analyzer->getFilteringEngine()) {
        analyzer->getFilteringEngine()->getStats().print();
    }
    
    // 显示SVF统计
    if (config.enable_svf_analysis && analyzer->getSVFAnalyzer()) {
        analyzer->getSVFAnalyzer()->printAnalysisStatistics();
    }
    
    // 安全清理 - 明确的清理顺序
    results.clear();
    
    // 先清理analyzer，再清理Context
    delete analyzer;
    analyzer = nullptr;
    
    delete Context;
    Context = nullptr;
    
    outs() << "✅ Cross-module analysis completed successfully!\n";
    
    return 0;
}
