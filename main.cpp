//===- main.cpp - SVF中断处理分析器主程序 (修复版) ------------------------===//

#include "SVFAnalyzer.h"
#include "SVFJSONOutput.h" 
#include "CompileCommandsParser.h"
#include "IRQHandlerIdentifier.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 简化的配置结构
//===----------------------------------------------------------------------===//

struct Config {
    std::string compile_commands;
    std::string handlers;
    std::string output;
    std::string report;
    size_t max_modules;
    bool verbose;
    bool generate_reports;
    bool help;
    
    Config() : output("svf_results.json"), report("svf_report.md"), 
               max_modules(0), verbose(false), generate_reports(false), help(false) {}
};

//===----------------------------------------------------------------------===//
// 命令行解析
//===----------------------------------------------------------------------===//

void printUsage(const char* prog) {
    outs() << "SVF Interrupt Handler Analyzer\n";
    outs() << "=============================\n\n";
    outs() << "Usage: " << prog << " [options]\n\n";
    outs() << "Required:\n";
    outs() << "  --compile-commands=<file>   compile_commands.json\n";
    outs() << "  --handlers=<file>           handler.json\n\n";
    outs() << "Optional:\n";
    outs() << "  --output=<file>             Output JSON (default: svf_results.json)\n";
    outs() << "  --report=<file>             Markdown report (default: svf_report.md)\n";
    outs() << "  --max-modules=<n>           Limit modules\n";
    outs() << "  --generate-reports          Generate markdown reports\n";
    outs() << "  --verbose                   Detailed output\n";
    outs() << "  --help                      This help\n\n";
    outs() << "Examples:\n";
    outs() << "  " << prog << " --compile-commands=cc.json --handlers=h.json\n";
    outs() << "  " << prog << " --compile-commands=cc.json --handlers=h.json --verbose\n";
}

bool parseArgs(int argc, char** argv, Config& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            config.help = true;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--generate-reports") {
            config.generate_reports = true;
        } else if (arg.find("--compile-commands=") == 0) {
            config.compile_commands = arg.substr(19);
        } else if (arg.find("--handlers=") == 0) {
            config.handlers = arg.substr(11);
        } else if (arg.find("--output=") == 0) {
            config.output = arg.substr(9);
        } else if (arg.find("--report=") == 0) {
            config.report = arg.substr(9);
        } else if (arg.find("--max-modules=") == 0) {
            config.max_modules = std::stoull(arg.substr(14));
        } else {
            errs() << "Unknown option: " << arg << "\n";
            return false;
        }
    }
    
    if (!config.help && (config.compile_commands.empty() || config.handlers.empty())) {
        errs() << "Missing required arguments\n";
        return false;
    }
    
    return true;
}

//===----------------------------------------------------------------------===//
// 输入验证
//===----------------------------------------------------------------------===//

struct InputValidation {
    bool success;
    std::vector<std::string> bc_files;
    std::vector<std::string> handler_names;
    std::string error;
    
    InputValidation() : success(false) {}
};

InputValidation validateInputs(const Config& config) {
    InputValidation result;
    
    // 检查文件存在
    if (!sys::fs::exists(config.compile_commands)) {
        result.error = "File not found: " + config.compile_commands;
        return result;
    }
    
    if (!sys::fs::exists(config.handlers)) {
        result.error = "File not found: " + config.handlers;
        return result;
    }
    
    // 解析compile_commands.json
    CompileCommandsParser parser;
    if (!parser.parseFromFile(config.compile_commands)) {
        result.error = "Failed to parse: " + config.compile_commands;
        return result;
    }
    
    // 获取bitcode文件
    auto all_bc = parser.getBitcodeFiles();
    for (const auto& bc : all_bc) {
        if (sys::fs::exists(bc)) {
            result.bc_files.push_back(bc);
        }
    }
    
    if (result.bc_files.empty()) {
        result.error = "No .bc files found. Run: make prepare-bitcode";
        return result;
    }
    
    // 限制模块数量
    if (config.max_modules > 0 && result.bc_files.size() > config.max_modules) {
        result.bc_files.resize(config.max_modules);
    }
    
    // 解析handlers - 使用公共接口
    InterruptHandlerIdentifier identifier;
    
    // 首先尝试解析JSON文件（通过loadHandlersFromJson会内部调用parseHandlerJsonFile）
    LLVMContext dummy_context;
    Module dummy_module("dummy", dummy_context);
    
    if (!identifier.loadHandlersFromJson(config.handlers, dummy_module)) {
        result.error = "Failed to parse: " + config.handlers;
        return result;
    }
    
    result.handler_names = identifier.getHandlerNames();
    if (result.handler_names.empty()) {
        result.error = "No handlers found in: " + config.handlers;
        return result;
    }
    
    result.success = true;
    return result;
}

//===----------------------------------------------------------------------===//
// 主程序
//===----------------------------------------------------------------------===//

int main(int argc, char** argv) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 解析参数
    Config config;
    if (!parseArgs(argc, argv, config)) {
        return 1;
    }
    
    if (config.help) {
        printUsage(argv[0]);
        return 0;
    }
    
    // 检查SVF
    if (!SVFAnalyzer::isSVFAvailable()) {
        errs() << "❌ SVF not available!\n";
        errs() << "Please install SVF and rebuild.\n";
        return 1;
    }
    
    // 显示配置
    outs() << "🔍 SVF Interrupt Handler Analyzer\n";
    outs() << "📁 Input: " << config.compile_commands << " + " << config.handlers << "\n";
    outs() << "📄 Output: " << config.output << "\n";
    if (config.generate_reports) {
        outs() << "📊 Report: " << config.report << "\n";
    }
    outs() << "\n";
    
    // 验证输入
    auto validation = validateInputs(config);
    if (!validation.success) {
        errs() << "❌ " << validation.error << "\n";
        return 1;
    }
    
    if (config.verbose) {
        outs() << "✅ Found " << validation.bc_files.size() << " bitcode files\n";
        outs() << "✅ Found " << validation.handler_names.size() << " handlers\n\n";
    }
    
    // 创建分析器
    LLVMContext context;
    SVFIRQAnalyzer analyzer(&context);
    
    // 加载模块
    outs() << "📦 Loading modules...\n";
    if (!analyzer.loadModules(validation.bc_files)) {
        errs() << "❌ Failed to load modules\n";
        return 1;
    }
    
    // 运行SVF分析
    outs() << "🔬 Running SVF analysis...\n";
    auto results = analyzer.analyzeAllHandlers(validation.handler_names);
    
    if (results.empty()) {
        outs() << "⚠️  No handlers found in loaded modules\n";
        return 1;
    }
    
    // 输出结果
    outs() << "💾 Generating output...\n";
    SVFJSONOutputGenerator json_output;
    json_output.outputResults(results, config.output);
    
    // 生成报告
    if (config.generate_reports) {
        outs() << "📊 Generating reports...\n";
        SVFReportGenerator report_gen;
        report_gen.generateMarkdownReport(results, config.report);
        report_gen.generateFunctionPointerSummary(results, "function_pointers.md");
        report_gen.generateStructUsageReport(results, "struct_usage.md");
    }
    
    // 显示结果统计
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    size_t total_fp_calls = 0;
    size_t total_structs = 0;
    size_t total_patterns = 0;
    double avg_precision = 0.0;
    
    for (const auto& result : results) {
        total_fp_calls += result.function_pointer_calls.size();
        total_structs += result.struct_usage.size();
        total_patterns += result.access_patterns.size();
        avg_precision += result.svf_precision_score;
    }
    
    if (!results.empty()) {
        avg_precision /= results.size();
    }
    
    outs() << "\n🎯 Analysis Results:\n";
    outs() << "  Handlers: " << results.size() << "\n";
    outs() << "  Function pointer calls: " << total_fp_calls << "\n";
    outs() << "  Struct types: " << total_structs << "\n";
    outs() << "  Access patterns: " << total_patterns << "\n";
    
    // 修复格式化输出问题 - 使用format方法
    outs() << "  Avg precision: " << format("%.1f", avg_precision) << "\n";
    outs() << "⏱️  Time: " << duration.count() << " ms\n";
    outs() << "📁 Output: " << config.output << "\n";
    
    if (config.verbose && analyzer.getSVFAnalyzer()) {
        analyzer.getSVFAnalyzer()->printStatistics();
    }
    
    outs() << "✅ Analysis completed!\n";
    return 0;
}
