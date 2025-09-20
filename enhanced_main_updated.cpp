//===- enhanced_main_updated.cpp - 使用简化JSON的增强主程序 --------------===//

#include "EnhancedCrossModuleAnalyzer.h"
#include "SimpleEnhancedJSONOutput.h"  // 使用简化版本
#include "CompileCommandsParser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include <string>
#include <vector>
#include <chrono>
#include <csignal>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 简化版本的配置
//===----------------------------------------------------------------------===//

struct EnhancedAnalysisConfig {
    std::string compile_commands_path;
    std::string handler_json_path;
    std::string output_path;
    std::string report_dir;
    size_t max_modules;
    bool verbose;
    bool show_stats;
    bool show_help;
    bool enable_svf;
    bool enable_deep_analysis;
    bool generate_reports;  // 简化的报告
    bool compare_with_basic;
    int svf_analysis_timeout;
    
    EnhancedAnalysisConfig() : 
        output_path("enhanced_analysis_results.json"),
        report_dir("analysis_reports"),
        max_modules(0),
        verbose(false),
        show_stats(false), 
        show_help(false),
        enable_svf(true),
        enable_deep_analysis(true),
        generate_reports(false),
        compare_with_basic(false),
        svf_analysis_timeout(600) {}
};

//===----------------------------------------------------------------------===//
// 信号处理（简化版）
//===----------------------------------------------------------------------===//

volatile sig_atomic_t signal_received = 0;
EnhancedCrossModuleAnalyzer* global_analyzer = nullptr;

void enhancedSignalHandler(int sig) {
    signal_received = sig;
    errs() << "\n💥 程序收到信号 " << sig << "，正在退出...\n";
    if (global_analyzer) {
        errs() << "清理分析器资源...\n";
    }
    std::_Exit(128 + sig);
}

void setupEnhancedSignalHandlers() {
    std::signal(SIGSEGV, enhancedSignalHandler);
    std::signal(SIGABRT, enhancedSignalHandler);
    std::signal(SIGINT, enhancedSignalHandler);
    std::signal(SIGTERM, enhancedSignalHandler);
}

//===----------------------------------------------------------------------===//
// 简化的命令行参数解析
//===----------------------------------------------------------------------===//

void printSimpleUsage(const char* program_name) {
    outs() << "Enhanced IRQ Handler Analyzer with SVF Integration\n";
    outs() << "================================================\n\n";
    
    outs() << "Usage: " << program_name << " [options]\n\n";
    
    outs() << "Required:\n";
    outs() << "  --compile-commands=<file>  compile_commands.json file\n";
    outs() << "  --handlers=<file>          handler.json file\n\n";
    
    outs() << "Optional:\n";
    outs() << "  --output=<file>            Output JSON file\n";
    outs() << "  --report-dir=<dir>         Report directory\n";
    outs() << "  --generate-reports         Generate simple reports\n";
    outs() << "  --compare-with-basic       Compare with basic analysis\n";
    outs() << "  --max-modules=<n>          Limit module count\n";
    outs() << "  --disable-svf              Disable SVF\n";
    outs() << "  --verbose                  Detailed output\n";
    outs() << "  --stats                    Show statistics\n";
    outs() << "  --help                     This help\n\n";
    
    outs() << "Examples:\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json --generate-reports\n";
}

bool parseSimpleCommandLineArgs(int argc, char** argv, EnhancedAnalysisConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            config.show_help = true;
            return true;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--stats") {
            config.show_stats = true;
        } else if (arg == "--disable-svf") {
            config.enable_svf = false;
        } else if (arg == "--generate-reports") {
            config.generate_reports = true;
        } else if (arg == "--compare-with-basic") {
            config.compare_with_basic = true;
        } else if (arg.find("--compile-commands=") == 0) {
            config.compile_commands_path = arg.substr(19);
        } else if (arg.find("--handlers=") == 0) {
            config.handler_json_path = arg.substr(11);
        } else if (arg.find("--output=") == 0) {
            config.output_path = arg.substr(9);
        } else if (arg.find("--report-dir=") == 0) {
            config.report_dir = arg.substr(13);
        } else if (arg.find("--max-modules=") == 0) {
            std::string value = arg.substr(14);
            config.max_modules = std::stoull(value);
        } else {
            errs() << "Unknown argument: " << arg << "\n";
            return false;
        }
    }
    
    if (!config.show_help) {
        if (config.compile_commands_path.empty() || config.handler_json_path.empty()) {
            errs() << "Missing required arguments\n";
            return false;
        }
    }
    
    return true;
}

//===----------------------------------------------------------------------===//
// 简化的输入验证
//===----------------------------------------------------------------------===//

struct SimpleValidationResult {
    bool success;
    std::vector<std::string> bc_files;
    size_t total_commands;
    std::string error_message;
    
    SimpleValidationResult() : success(false), total_commands(0) {}
};

SimpleValidationResult validateSimpleInputs(const EnhancedAnalysisConfig& config) {
    SimpleValidationResult result;
    
    // 检查文件存在
    if (!sys::fs::exists(config.compile_commands_path)) {
        result.error_message = "compile_commands.json not found";
        return result;
    }
    
    if (!sys::fs::exists(config.handler_json_path)) {
        result.error_message = "handler.json not found";
        return result;
    }
    
    // 解析compile_commands.json
    CompileCommandsParser parser;
    if (!parser.parseFromFile(config.compile_commands_path)) {
        result.error_message = "Failed to parse compile_commands.json";
        return result;
    }
    
    result.total_commands = parser.getCommandCount();
    if (result.total_commands == 0) {
        result.error_message = "No compile commands found";
        return result;
    }
    
    // 获取bitcode文件
    std::vector<std::string> all_bc_files = parser.getBitcodeFiles();
    for (const auto& bc_file : all_bc_files) {
        if (sys::fs::exists(bc_file)) {
            result.bc_files.push_back(bc_file);
        }
    }
    
    if (result.bc_files.empty()) {
        result.error_message = "No bitcode files found";
        return result;
    }
    
    // 应用限制
    if (config.max_modules > 0 && result.bc_files.size() > config.max_modules) {
        result.bc_files.resize(config.max_modules);
    }
    
    result.success = true;
    return result;
}

//===----------------------------------------------------------------------===//
// 简化的统计信息
//===----------------------------------------------------------------------===//

struct SimpleStatistics {
    std::chrono::milliseconds total_time;
    size_t handlers_found;
    size_t svf_enhanced_handlers;
    double average_precision;
    bool svf_enabled;
    
    SimpleStatistics() : total_time(0), handlers_found(0), svf_enhanced_handlers(0),
                         average_precision(0.0), svf_enabled(false) {}
};

void collectSimpleStatistics(const EnhancedCrossModuleAnalyzer& analyzer,
                            const std::vector<EnhancedInterruptHandlerAnalysis>& results,
                            SimpleStatistics& stats) {
    stats.handlers_found = results.size();
    stats.svf_enabled = analyzer.isSVFEnabled();
    
    double total_precision = 0.0;
    for (const auto& analysis : results) {
        total_precision += analysis.analysis_precision_score;
        
        // 检查是否有SVF增强
        for (const auto& access : analysis.enhanced_memory_accesses) {
            if (access.svf_enhanced) {
                stats.svf_enhanced_handlers++;
                break;
            }
        }
    }
    
    if (!results.empty()) {
        stats.average_precision = total_precision / results.size();
    }
}

//===----------------------------------------------------------------------===//
// 主程序
//===----------------------------------------------------------------------===//

int main(int argc, char** argv) {
    setupEnhancedSignalHandlers();
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 解析参数
    EnhancedAnalysisConfig config;
    if (!parseSimpleCommandLineArgs(argc, argv, config)) {
        return 1;
    }
    
    if (config.show_help) {
        printSimpleUsage(argv[0]);
        return 0;
    }
    
    // 显示配置
    outs() << "Enhanced IRQ Analysis Tool\n";
    outs() << "Input: " << config.compile_commands_path << " + " << config.handler_json_path << "\n";
    outs() << "Output: " << config.output_path << "\n";
    if (config.generate_reports) {
        outs() << "Reports: " << config.report_dir << "\n";
    }
    outs() << "\n";
    
    // 验证输入
    SimpleValidationResult validation = validateSimpleInputs(config);
    if (!validation.success) {
        errs() << "Error: " << validation.error_message << "\n";
        return 1;
    }
    
    if (config.verbose) {
        outs() << "Found " << validation.bc_files.size() << " bitcode files\n";
        outs() << "SVF enabled: " << (config.enable_svf ? "yes" : "no") << "\n\n";
    }
    
    // 创建分析器
    LLVMContext Context;
    EnhancedCrossModuleAnalyzer analyzer;
    global_analyzer = &analyzer;
    
    // 配置分析器
    analyzer.setDeepStructAnalysis(config.enable_deep_analysis);
    analyzer.setPrecisePointerAnalysis(config.enable_svf);
    
    // 加载模块
    outs() << "Loading modules...\n";
    if (!analyzer.loadAllModules(validation.bc_files, Context)) {
        errs() << "Failed to load modules\n";
        return 1;
    }
    
    // 运行增强分析
    outs() << "Running enhanced analysis...\n";
    std::vector<EnhancedInterruptHandlerAnalysis> enhanced_results = 
        analyzer.analyzeAllHandlersEnhanced(config.handler_json_path);
    
    if (enhanced_results.empty()) {
        outs() << "⚠️  No interrupt handlers found!\n";
        return 1;
    }
    
    // 比较分析（如果启用）
    if (config.compare_with_basic) {
        outs() << "Running basic analysis for comparison...\n";
        auto basic_results = analyzer.analyzeAllHandlers(config.handler_json_path);
        
        if (!basic_results.empty()) {
            auto comparison = SimpleAnalysisComparator::compareAnalyses(basic_results, enhanced_results);
            SimpleAnalysisComparator::generateSimpleComparisonReport(comparison, 
                config.report_dir + "/comparison.md");
            outs() << "Comparison report generated\n";
        }
    }
    
    // 输出结果
    EnhancedJSONOutputGenerator json_generator;
    json_generator.outputEnhancedAnalysisResults(enhanced_results, config.output_path, true);
    
    // 生成简化报告（如果启用）
    if (config.generate_reports) {
        outs() << "Generating simple reports...\n";
        json_generator.generateSimpleReports(enhanced_results, config.report_dir);
    }
    
    // 收集和显示统计
    SimpleStatistics stats;
    auto end_time = std::chrono::high_resolution_clock::now();
    stats.total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    collectSimpleStatistics(analyzer, enhanced_results, stats);
    
    outs() << "\n=== Analysis Completed ===\n";
    outs() << "📊 Results:\n";
    outs() << "  Handlers analyzed: " << stats.handlers_found << "\n";
    outs() << "  SVF enhanced: " << stats.svf_enhanced_handlers << "\n";
    outs() << "  Average precision: " << std::fixed << std::setprecision(1) 
           << stats.average_precision << "\n";
    outs() << "⏱️  Total time: " << stats.total_time.count() << " ms\n";
    outs() << "📁 Output: " << config.output_path << "\n";
    
    if (config.show_stats) {
        json_generator.printSimpleStatistics(enhanced_results);
    }
    
    global_analyzer = nullptr;
    outs() << "✅ Analysis completed successfully!\n";
    
    return 0;
}
