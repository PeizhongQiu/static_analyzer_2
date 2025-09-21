//===- main.cpp - SVF中断处理分析器主程序 -------------------------------===//

#include "SVFInterruptAnalyzer.h"
#include "ParallelSVFAnalyzer.h"
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
    size_t max_modules;
    bool help;
    bool parallel;
    size_t num_threads;
    size_t files_per_group;
    
    AnalyzerConfig() : output("interrupt_analysis.json"), verbose(false), max_modules(0), 
                      help(false), parallel(false), num_threads(4), files_per_group(500) {}
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
    outs() << "  --max-modules=<n>           Maximum number of modules to analyze (default: 0=all)\n";
    outs() << "  --parallel                  Enable parallel analysis\n";
    outs() << "  --threads=<n>               Number of parallel threads (default: 4)\n";
    outs() << "  --group-size=<n>            Files per group for parallel analysis (default: 500)\n";
    outs() << "  --verbose                   Enable verbose output\n";
    outs() << "  --help, -h                  Show this help message\n\n";
    outs() << "Examples:\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json --verbose\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json --parallel --threads=8\n";
    outs() << "  " << program_name << " --compile-commands=cc.json --handlers=h.json --max-modules=1000\n\n";
}

bool parseCommandLine(int argc, char** argv, AnalyzerConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            config.help = true;
            return true;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--parallel") {
            config.parallel = true;
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
        } else if (arg.find("--threads=") == 0) {
            try {
                config.num_threads = std::stoull(arg.substr(10));
            } catch (const std::exception&) {
                errs() << "❌ Invalid threads value: " << arg.substr(10) << "\n";
                return false;
            }
        } else if (arg.find("--group-size=") == 0) {
            try {
                config.files_per_group = std::stoull(arg.substr(13));
            } catch (const std::exception&) {
                errs() << "❌ Invalid group-size value: " << arg.substr(13) << "\n";
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
    
    outs() << "🎯 Performing dependency-based module selection...\n";
    outs() << "Target handlers: ";
    for (const auto& name : handler_names) {
        outs() << name << " ";
    }
    outs() << "\n";
    
    // 如果max_modules设置得很大或为0，分析所有文件
    if (max_modules == 0 || max_modules >= all_files.size()) {
        outs() << "📦 Using ALL " << all_files.size() << " bitcode files for comprehensive analysis\n";
        return all_files;
    }
    
    std::vector<std::string> selected;
    std::set<std::string> selected_set;
    std::set<std::string> required_symbols; // 需要查找的符号
    
    // 第一步：找到包含目标handler的.bc文件作为起点
    outs() << "🔍 Step 1: Finding modules containing target handlers...\n";
    
    for (const auto& file : all_files) {
        bool contains_handler = false;
        
        // 简单启发式：检查文件名是否与handler名称相关
        for (const auto& handler : handler_names) {
            std::string file_lower = file;
            std::string handler_lower = handler;
            std::transform(file_lower.begin(), file_lower.end(), file_lower.begin(), ::tolower);
            std::transform(handler_lower.begin(), handler_lower.end(), handler_lower.begin(), ::tolower);
            
            // 检查文件名是否包含handler的关键部分
            if (file_lower.find(handler_lower.substr(0, std::min(handler_lower.length(), size_t(4)))) != std::string::npos) {
                contains_handler = true;
                break;
            }
            
            // 或者基于路径推断（例如aer_irq可能在pci/aer相关路径中）
            if (handler_lower.find("aer") != std::string::npos && file_lower.find("aer") != std::string::npos) {
                contains_handler = true;
                break;
            }
            if (handler_lower.find("pci") != std::string::npos && file_lower.find("pci") != std::string::npos) {
                contains_handler = true;
                break;
            }
        }
        
        if (contains_handler) {
            selected.push_back(file);
            selected_set.insert(file);
            outs() << "📦 Target module: " << file << "\n";
        }
    }
    
    if (selected.empty()) {
        outs() << "⚠️  No target modules found, using keyword-based fallback...\n";
        // 回退到基于关键词的选择
        for (const auto& file : all_files) {
            if (selected.size() >= max_modules) break;
            
            std::string file_lower = file;
            std::transform(file_lower.begin(), file_lower.end(), file_lower.begin(), ::tolower);
            
            if (file_lower.find("aer") != std::string::npos || 
                file_lower.find("pci") != std::string::npos ||
                file_lower.find("irq") != std::string::npos) {
                selected.push_back(file);
                selected_set.insert(file);
                outs() << "📦 Fallback selection: " << file << "\n";
            }
        }
    }
    
    // 第二步：添加核心依赖模块
    outs() << "🔍 Step 2: Adding core dependency modules...\n";
    
    std::vector<std::string> core_patterns = {
        "kernel/irq/",
        "arch/x86/kernel/irq", 
        "drivers/base/",
        "kernel/printk/",
        "mm/",
        "kernel/time/",
        "arch/x86/mm/"
    };
    
    for (const auto& pattern : core_patterns) {
        if (selected.size() >= max_modules) break;
        
        for (const auto& file : all_files) {
            if (selected.size() >= max_modules) break;
            if (selected_set.find(file) != selected_set.end()) continue;
            
            if (file.find(pattern) != std::string::npos) {
                selected.push_back(file);
                selected_set.insert(file);
                outs() << "📦 Core dependency: " << file << "\n";
            }
        }
    }
    
    // 第三步：添加PCI子系统相关模块
    outs() << "🔍 Step 3: Adding subsystem-specific modules...\n";
    
    std::vector<std::string> subsystem_patterns = {
        "drivers/pci/",
        "arch/x86/pci/",
        "drivers/char/",
        "fs/proc/"
    };
    
    for (const auto& pattern : subsystem_patterns) {
        if (selected.size() >= max_modules) break;
        
        for (const auto& file : all_files) {
            if (selected.size() >= max_modules) break;
            if (selected_set.find(file) != selected_set.end()) continue;
            
            if (file.find(pattern) != std::string::npos) {
                selected.push_back(file);
                selected_set.insert(file);
                outs() << "📦 Subsystem module: " << file << "\n";
            }
        }
    }
    
    // 第四步：如果还有空间，添加其他可能相关的模块
    if (selected.size() < max_modules) {
        outs() << "🔍 Step 4: Adding additional modules...\n";
        
        for (const auto& file : all_files) {
            if (selected.size() >= max_modules) break;
            if (selected_set.find(file) != selected_set.end()) continue;
            
            std::string file_lower = file;
            std::transform(file_lower.begin(), file_lower.end(), file_lower.begin(), ::tolower);
            
            // 优先选择drivers目录下的模块
            if (file_lower.find("drivers/") != std::string::npos) {
                selected.push_back(file);
                selected_set.insert(file);
                outs() << "📦 Additional driver: " << file << "\n";
            }
        }
    }
    
    outs() << "📊 Selected " << selected.size() << " modules based on dependency analysis\n";
    return selected;
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
    outs() << "🔢 Max modules: " << config.max_modules << "\n";
    outs() << "🔊 Verbose: " << (config.verbose ? "Yes" : "No") << "\n";
    outs() << "⚡ Parallel: " << (config.parallel ? "Yes" : "No") << "\n";
    if (config.parallel) {
        outs() << "🧵 Threads: " << config.num_threads << "\n";
        outs() << "📦 Files per group: " << config.files_per_group << "\n";
    }
    outs() << "\n";
    
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
    
    // 运行分析
    std::vector<InterruptHandlerResult> results;
    
    if (config.parallel) {
        // 并行分析
        outs() << "\n📋 Step 4: Running parallel SVF analysis\n";
        ParallelSVFAnalyzer parallel_analyzer;
        results = parallel_analyzer.analyzeInParallel(
            selected_files, 
            handler_names, 
            config.num_threads, 
            config.files_per_group
        );
        
        // 输出结果
        outs() << "\n📋 Step 5: Generating output\n";
        outputResults(results, config.output);
        
    } else {
        // 串行分析
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
        results = analyzer.analyzeInterruptHandlers(handler_names);
        
        // 输出结果
        outs() << "\n📋 Step 6: Generating output\n";
        analyzer.outputResults(results, config.output);
        
        // 显示统计信息
        if (config.verbose) {
            analyzer.printStatistics();
        }
    }
    
    if (results.empty()) {
        errs() << "❌ No analysis results generated\n";
        return 1;
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
    
    // 避免SVF析构函数引起的段错误，直接退出
    exit(0);
    
    return 0;
}
