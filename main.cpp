//===- main.cpp - SVF中断处理分析器主程序 -------------------------------===//

#include "SVFInterruptAnalyzer.h"
#include "CompileCommandsParser.h"
#include "IRQHandlerIdentifier.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 配置结构
//===----------------------------------------------------------------------===//

struct AnalyzerConfig {
    std::string compile_commands;
    std::string handlers;
    std::string output;
    bool verbose;
    bool help;
    
    AnalyzerConfig() : output("interrupt_analysis.json"), verbose(false), help(false) {}
};

//===----------------------------------------------------------------------===//
// 命令行解析
//===----------------------------------------------------------------------===//

void printUsage(const char* program_name) {
    outs() << "SVF Interrupt Handler Analyzer\n";
    outs() << "==============================\n\n";
    outs() << "Usage: " << program_name << " [options]\n\n";
    outs() << "Required options:\n";
    outs() << "  --compile-commands=<file>   Path to compile_commands.json\n";
    outs() << "  --handlers=<file>           Path to handler.json\n\n";
    outs() << "Optional options:\n";
    outs() << "  --output=<file>             Output JSON file (default: interrupt_analysis.json)\n";
    outs() << "  --verbose                   Enable verbose output\n";
    outs() << "  --help, -h                  Show this help message\n\n";
    outs() << "Analysis Mode:\n";
    outs() << "  Serial mode:   Full SVF analysis in single thread (recommended)\n\n";
    outs() << "Examples:\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json --verbose\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json --output=results.json\n\n";
}

bool parseCommandLine(int argc, char** argv, AnalyzerConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            config.help = true;
            return true;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg.find("--compile-commands=") == 0) {
            config.compile_commands = arg.substr(19);
        } else if (arg.find("--handlers=") == 0) {
            config.handlers = arg.substr(11);
        } else if (arg.find("--output=") == 0) {
            config.output = arg.substr(9);
        } else {
            errs() << "❌ Unknown option: " << arg << "\n";
            return false;
        }
    }
    
    if (!config.help && (config.compile_commands.empty() || config.handlers.empty())) {
        errs() << "❌ Missing required arguments\n";
        return false;
    }
    
    return true;
}

//===----------------------------------------------------------------------===//
// 输入验证
//===----------------------------------------------------------------------===//

bool validateInputs(const AnalyzerConfig& config) {
    if (!sys::fs::exists(config.compile_commands)) {
        errs() << "❌ File not found: " << config.compile_commands << "\n";
        return false;
    }
    
    if (!sys::fs::exists(config.handlers)) {
        errs() << "❌ File not found: " << config.handlers << "\n";
        return false;
    }
    
    return true;
}

//===----------------------------------------------------------------------===//
// 输出结果的辅助函数
//===----------------------------------------------------------------------===//

void outputResults(const std::vector<InterruptHandlerResult>& results, const std::string& output_file) {
    // 创建一个临时分析器来使用其输出函数
    LLVMContext temp_context;
    SVFInterruptAnalyzer temp_analyzer(&temp_context);
    temp_analyzer.outputResults(results, output_file);
}

//===----------------------------------------------------------------------===//
// 主程序
//===----------------------------------------------------------------------===//

int main(int argc, char** argv) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 解析命令行参数
    AnalyzerConfig config;
    if (!parseCommandLine(argc, argv, config)) {
        printUsage(argv[0]);
        return 1;
    }
    
    if (config.help) {
        printUsage(argv[0]);
        return 0;
    }
    
    // 显示配置
    outs() << "🚀 SVF Interrupt Handler Analyzer\n";
    outs() << "==================================\n";
    outs() << "📁 Compile commands: " << config.compile_commands << "\n";
    outs() << "📄 Handlers file: " << config.handlers << "\n";
    outs() << "📊 Output file: " << config.output << "\n";
    outs() << "🔊 Verbose: " << (config.verbose ? "Yes" : "No") << "\n";
    outs() << "⚡ Mode: Serial SVF analysis (single-threaded)\n";
    outs() << "\n";
    
    // 验证输入文件
    if (!validateInputs(config)) {
        return 1;
    }
    
    // 解析compile_commands.json
    outs() << "📋 Step 1: Parsing compile_commands.json\n";
    outs() << "🔍 Reading file: " << config.compile_commands << "\n";
    
    CompileCommandsParser cc_parser;
    if (!cc_parser.parseFromFile(config.compile_commands)) {
        errs() << "❌ Failed to parse compile_commands.json\n";
        return 1;
    }
    
    auto all_bc_files = cc_parser.getBitcodeFiles();
    outs() << "✅ Found " << all_bc_files.size() << " potential bitcode files\n";
    
    // 过滤存在的文件
    outs() << "🔍 Checking file existence...\n";
    std::vector<std::string> existing_files;
    size_t checked = 0;
    size_t progress_interval = std::max(size_t(1), all_bc_files.size() / 10);
    
    for (size_t i = 0; i < all_bc_files.size(); ++i) {
        const auto& file = all_bc_files[i];
        
        // 显示检查进度
        if (i % progress_interval == 0 && all_bc_files.size() > 100) {
            size_t percentage = (i * 100) / all_bc_files.size();
            outs() << "📁 Checking files: " << percentage << "% (" << i << "/" << all_bc_files.size() << ")\n";
        }
        
        if (sys::fs::exists(file)) {
            existing_files.push_back(file);
        }
        checked++;
    }
    
    outs() << "✅ " << existing_files.size() << " files exist on disk (checked " << checked << " files)\n";
    
    if (existing_files.empty()) {
        errs() << "❌ No bitcode files found. Please ensure .bc files are generated.\n";
        return 1;
    }
    
    // 解析handlers.json
    outs() << "\n📋 Step 2: Parsing handlers.json\n";
    outs() << "🔍 Reading file: " << config.handlers << "\n";
    
    InterruptHandlerIdentifier handler_parser;
    if (!handler_parser.parseHandlerJsonFile(config.handlers)) {
        errs() << "❌ Failed to parse handlers.json\n";
        return 1;
    }
    
    auto handler_names = handler_parser.getHandlerNames();
    if (handler_names.empty()) {
        errs() << "❌ No handlers found in handlers.json\n";
        return 1;
    }
    
    outs() << "✅ Found " << handler_names.size() << " handlers to analyze\n";
    if (config.verbose) {
        outs() << "🎯 Target handlers: ";
        for (const auto& name : handler_names) {
            outs() << name << " ";
        }
        outs() << "\n";
    }
    
    // 选择要分析的模块 - 使用所有可用文件
    outs() << "\n📋 Step 3: Preparing modules for analysis\n";
    auto selected_files = existing_files;  // 直接使用所有存在的文件
    
    outs() << "📦 Using ALL " << selected_files.size() << " available bitcode files\n";
    
    if (selected_files.empty()) {
        errs() << "❌ No modules available for analysis\n";
        return 1;
    }
    
    // 运行分析
    std::vector<InterruptHandlerResult> results;
    
    // 串行分析 - 完整SVF功能
    outs() << "\n📋 Step 4: Initializing SVF analyzer\n";
    outs() << "💡 Using full SVF analysis with complete pointer tracking\n";
    outs() << "📊 Analysis will process " << selected_files.size() << " bitcode files\n";
    
    auto svf_init_start = std::chrono::high_resolution_clock::now();
    
    LLVMContext context;
    SVFInterruptAnalyzer analyzer(&context);
    
    // 加载bitcode文件
    outs() << "🔄 Phase 1: Loading bitcode files...\n";
    if (!analyzer.loadBitcodeFiles(selected_files)) {
        errs() << "❌ Failed to load bitcode files\n";
        return 1;
    }
    
    auto loading_end = std::chrono::high_resolution_clock::now();
    auto loading_duration = std::chrono::duration_cast<std::chrono::seconds>(loading_end - svf_init_start);
    outs() << "✅ File loading completed in " << loading_duration.count() << " seconds\n";
    
    // 初始化SVF
    outs() << "🔄 Phase 2: Initializing SVF framework...\n";
    if (!analyzer.initializeSVF()) {
        errs() << "❌ Failed to initialize SVF\n";
        return 1;
    }
    
    auto svf_init_end = std::chrono::high_resolution_clock::now();
    auto svf_init_duration = std::chrono::duration_cast<std::chrono::seconds>(svf_init_end - loading_end);
    outs() << "✅ SVF initialization completed in " << svf_init_duration.count() << " seconds\n";
    
    // 运行分析
    outs() << "\n📋 Step 5: Running interrupt handler analysis\n";
    outs() << "🔄 Phase 3: Analyzing " << handler_names.size() << " interrupt handlers...\n";
    
    auto analysis_start = std::chrono::high_resolution_clock::now();
    results = analyzer.analyzeInterruptHandlers(handler_names);
    auto analysis_end = std::chrono::high_resolution_clock::now();
    auto analysis_duration = std::chrono::duration_cast<std::chrono::seconds>(analysis_end - analysis_start);
    
    outs() << "✅ Handler analysis completed in " << analysis_duration.count() << " seconds\n";
    
    // 输出结果
    outs() << "\n📋 Step 6: Generating output\n";
    analyzer.outputResults(results, config.output);
    
    // 显示统计信息
    if (config.verbose) {
        analyzer.printStatistics();
    }
    
    if (results.empty()) {
        errs() << "❌ No analysis results generated\n";
        return 1;
    }
    
    // 显示摘要
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    outs() << "\n📈 Analysis Summary\n";
    outs() << "==================\n";
    
    size_t successful = 0;
    size_t with_indirect_calls = 0;
    size_t with_device_access = 0;
    double total_confidence = 0.0;
    
    for (const auto& result : results) {
        if (result.analysis_complete) successful++;
        if (!result.indirect_call_targets.empty()) with_indirect_calls++;
        if (result.has_device_access) with_device_access++;
        total_confidence += result.confidence_score;
    }
    
    double avg_confidence = results.empty() ? 0.0 : total_confidence / results.size();
    
    outs() << "📊 Performance Metrics:\n";
    outs() << "  ⏱️  Total analysis time: " << total_duration.count() << " seconds\n";
    outs() << "  📁 Files processed: " << selected_files.size() << "\n";
    outs() << "  📈 Throughput: " << (selected_files.size() / std::max(total_duration.count(), 1L)) << " files/second\n";
    outs() << "\n📈 Analysis Results:\n";
    outs() << "  ✅ Successfully analyzed: " << successful << "/" << results.size() << " handlers\n";
    outs() << "  🎯 Handlers with indirect calls: " << with_indirect_calls << "\n";
    outs() << "  🔧 Handlers with device access: " << with_device_access << "\n";
    char confidence_str[32];
    snprintf(confidence_str, sizeof(confidence_str), "%.1f", avg_confidence);
    outs() << "  📊 Average confidence: " << confidence_str << "/100\n";
    outs() << "  📁 Results saved to: " << config.output << "\n";
    
    if (config.verbose && successful > 0) {
        outs() << "\n📋 Detailed Results:\n";
        for (const auto& result : results) {
            if (result.analysis_complete) {
                outs() << "🔍 " << result.function_name << ":\n";
                outs() << "  📊 Instructions: " << result.total_instructions << "\n";
                outs() << "  📞 Function calls: " << result.function_calls << "\n";
                outs() << "  🎯 Indirect calls: " << result.indirect_calls << "\n";
                outs() << "  📈 Confidence: " << result.confidence_score << "/100\n";
                
                if (!result.indirect_call_targets.empty()) {
                    outs() << "  🎯 Indirect call targets:\n";
                    for (const auto& target : result.indirect_call_targets) {
                        outs() << "    -> " << target << "\n";
                    }
                }
                outs() << "\n";
            }
        }
    }
    
    outs() << "🎉 Analysis completed successfully!\n";
    
    // 正常退出
    exit(0);
    return 0;
}
