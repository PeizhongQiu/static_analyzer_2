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

using namespace llvm;

//===----------------------------------------------------------------------===//
// 配置结构
//===----------------------------------------------------------------------===//

struct AnalyzerConfig {
    std::string compile_commands;
    std::string handlers;
    std::string output;
    bool verbose;
    size_t max_modules;
    bool help;
    
    AnalyzerConfig() : output("interrupt_analysis.json"), verbose(false), max_modules(50), help(false) {}
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
    outs() << "  --max-modules=<n>           Maximum number of modules to analyze (default: 50)\n";
    outs() << "  --verbose                   Enable verbose output\n";
    outs() << "  --help, -h                  Show this help message\n\n";
    outs() << "Examples:\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json --verbose\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json --max-modules=20\n\n";
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
        } else if (arg.find("--max-modules=") == 0) {
            try {
                config.max_modules = std::stoull(arg.substr(14));
            } catch (const std::exception&) {
                errs() << "❌ Invalid max-modules value: " << arg.substr(14) << "\n";
                return false;
            }
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
// 模块选择策略
//===----------------------------------------------------------------------===//

std::vector<std::string> selectModulesForAnalysis(
    const std::vector<std::string>& all_files, 
    const std::vector<std::string>& handler_names,
    size_t max_modules) {
    
    outs() << "🎯 Selecting modules for analysis...\n";
    outs() << "Target handlers: ";
    for (const auto& name : handler_names) {
        outs() << name << " ";
    }
    outs() << "\n";
    
    std::vector<std::string> selected;
    std::set<std::string> selected_set; // 避免重复
    
    // 策略1: 基于handler名称推断相关模块
    for (const auto& handler : handler_names) {
        std::string handler_lower = handler;
        std::transform(handler_lower.begin(), handler_lower.end(), handler_lower.begin(), ::tolower);
        
        // 提取handler的关键词
        std::vector<std::string> keywords;
        if (handler_lower.find("aer") != std::string::npos) {
            keywords = {"aer", "pci", "pcie"};
        } else if (handler_lower.find("pci") != std::string::npos) {
            keywords = {"pci", "pcie"};
        } else if (handler_lower.find("usb") != std::string::npos) {
            keywords = {"usb"};
        } else if (handler_lower.find("net") != std::string::npos) {
            keywords = {"net", "eth"};
        } else {
            // 通用关键词
            keywords = {"irq", "interrupt"};
        }
        
        // 查找匹配的文件
        for (const auto& file : all_files) {
            if (selected.size() >= max_modules) break;
            if (selected_set.find(file) != selected_set.end()) continue;
            
            std::string file_lower = file;
            std::transform(file_lower.begin(), file_lower.end(), file_lower.begin(), ::tolower);
            
            for (const auto& keyword : keywords) {
                if (file_lower.find(keyword) != std::string::npos) {
                    selected.push_back(file);
                    selected_set.insert(file);
                    outs() << "📦 Handler-related: " << file << "\n";
                    break;
                }
            }
        }
    }
    
    // 策略2: 添加核心系统模块
    if (selected.size() < max_modules) {
        std::vector<std::string> core_patterns = {
            "kernel/irq/", "arch/x86/kernel/irq", "drivers/base/", 
            "kernel/softirq", "kernel/workqueue"
        };
        
        for (const auto& pattern : core_patterns) {
            if (selected.size() >= max_modules) break;
            
            for (const auto& file : all_files) {
                if (selected.size() >= max_modules) break;
                if (selected_set.find(file) != selected_set.end()) continue;
                
                if (file.find(pattern) != std::string::npos) {
                    selected.push_back(file);
                    selected_set.insert(file);
                    outs() << "📦 Core system: " << file << "\n";
                }
            }
        }
    }
    
    // 策略3: 如果还有空间，添加其他驱动模块
    if (selected.size() < max_modules) {
        for (const auto& file : all_files) {
            if (selected.size() >= max_modules) break;
            if (selected_set.find(file) != selected_set.end()) continue;
            
            if (file.find("drivers/") != std::string::npos) {
                selected.push_back(file);
                selected_set.insert(file);
                outs() << "📦 Additional driver: " << file << "\n";
            }
        }
    }
    
    outs() << "📊 Selected " << selected.size() << " modules for analysis\n";
    return selected;
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
    outs() << "🔢 Max modules: " << config.max_modules << "\n";
    outs() << "🔊 Verbose: " << (config.verbose ? "Yes" : "No") << "\n\n";
    
    // 验证输入文件
    if (!validateInputs(config)) {
        return 1;
    }
    
    // 解析compile_commands.json
    outs() << "📋 Step 1: Parsing compile_commands.json\n";
    CompileCommandsParser cc_parser;
    if (!cc_parser.parseFromFile(config.compile_commands)) {
        errs() << "❌ Failed to parse compile_commands.json\n";
        return 1;
    }
    
    auto all_bc_files = cc_parser.getBitcodeFiles();
    outs() << "✅ Found " << all_bc_files.size() << " potential bitcode files\n";
    
    // 过滤存在的文件
    std::vector<std::string> existing_files;
    for (const auto& file : all_bc_files) {
        if (sys::fs::exists(file)) {
            existing_files.push_back(file);
        }
    }
    
    outs() << "✅ " << existing_files.size() << " files exist on disk\n";
    
    if (existing_files.empty()) {
        errs() << "❌ No bitcode files found. Please ensure .bc files are generated.\n";
        return 1;
    }
    
    // 解析handlers.json
    outs() << "\n📋 Step 2: Parsing handlers.json\n";
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
        outs() << "Handlers: ";
        for (const auto& name : handler_names) {
            outs() << name << " ";
        }
        outs() << "\n";
    }
    
    // 选择要分析的模块
    outs() << "\n📋 Step 3: Selecting modules for analysis\n";
    auto selected_files = selectModulesForAnalysis(existing_files, handler_names, config.max_modules);
    
    if (selected_files.empty()) {
        errs() << "❌ No modules selected for analysis\n";
        return 1;
    }
    
    // 初始化分析器
    outs() << "\n📋 Step 4: Initializing SVF analyzer\n";
    LLVMContext context;
    SVFInterruptAnalyzer analyzer(&context);
    
    // 加载bitcode文件
    if (!analyzer.loadBitcodeFiles(selected_files)) {
        errs() << "❌ Failed to load bitcode files\n";
        return 1;
    }
    
    // 初始化SVF
    if (!analyzer.initializeSVF()) {
        errs() << "❌ Failed to initialize SVF\n";
        return 1;
    }
    
    // 运行分析
    outs() << "\n📋 Step 5: Running interrupt handler analysis\n";
    auto results = analyzer.analyzeInterruptHandlers(handler_names);
    
    if (results.empty()) {
        errs() << "❌ No analysis results generated\n";
        return 1;
    }
    
    // 输出结果
    outs() << "\n📋 Step 6: Generating output\n";
    analyzer.outputResults(results, config.output);
    
    // 显示统计信息
    if (config.verbose) {
        analyzer.printStatistics();
    }
    
    // 显示摘要
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
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
    
    outs() << "✅ Successfully analyzed: " << successful << "/" << results.size() << " handlers\n";
    outs() << "🎯 Handlers with indirect calls: " << with_indirect_calls << "\n";
    outs() << "🔧 Handlers with device access: " << with_device_access << "\n";
    char confidence_str[32];
    snprintf(confidence_str, sizeof(confidence_str), "%.1f", avg_confidence);
    outs() << "📊 Average confidence: " << confidence_str << "/100\n";
    outs() << "⏱️  Total analysis time: " << duration.count() << " ms\n";
    outs() << "📁 Results saved to: " << config.output << "\n";
    
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
    return 0;
}
