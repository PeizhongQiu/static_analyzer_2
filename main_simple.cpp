//===- main_simple.cpp - Simplified Main Program ----------------------===//
//
// 简化版主程序，避免LLVM命令行选项冲突
//
//===----------------------------------------------------------------------===//

#include "CompileCommandsParser.h"
#include "IRQAnalysisPass.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Analysis/CallGraph.h"
#include <string>
#include <cstring>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 简单的参数解析函数
//===----------------------------------------------------------------------===//

struct ProgramArgs {
    std::string compile_commands_path;
    std::string handler_json_path;
    std::string output_path = "irq_analysis_results.json";
    bool verbose = false;
    bool show_stats = false;
    bool show_help = false;
};

void printUsage(const char* program_name) {
    outs() << "LLVM Interrupt Handler Analyzer\n";
    outs() << "Usage: " << program_name << " [options]\n";
    outs() << "\nRequired options:\n";
    outs() << "  --compile-commands=<file>  Path to compile_commands.json file\n";
    outs() << "  --handlers=<file>          Path to handler.json file\n";
    outs() << "\nOptional options:\n";
    outs() << "  --output=<file>            Output file (default: irq_analysis_results.json)\n";
    outs() << "  --verbose                  Enable verbose output\n";
    outs() << "  --stats                    Show analysis statistics\n";
    outs() << "  --help                     Show this help message\n";
    outs() << "\nExample:\n";
    outs() << "  " << program_name << " --compile-commands=compile_commands.json \\\n";
    outs() << "                      --handlers=handler.json \\\n";
    outs() << "                      --output=results.json --verbose\n";
}

bool parseArguments(int argc, char** argv, ProgramArgs& args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            args.show_help = true;
            return true;
        } else if (arg == "--verbose") {
            args.verbose = true;
        } else if (arg == "--stats") {
            args.show_stats = true;
        } else if (arg.find("--compile-commands=") == 0) {
            args.compile_commands_path = arg.substr(19);
        } else if (arg.find("--handlers=") == 0) {
            args.handler_json_path = arg.substr(11);
        } else if (arg.find("--output=") == 0) {
            args.output_path = arg.substr(9);
        } else {
            errs() << "Unknown argument: " << arg << "\n";
            return false;
        }
    }
    
    // 检查必需参数
    if (!args.show_help && (args.compile_commands_path.empty() || args.handler_json_path.empty())) {
        errs() << "Error: Missing required arguments\n";
        errs() << "Use --help for usage information\n";
        return false;
    }
    
    return true;
}

//===----------------------------------------------------------------------===//
// 主程序
//===----------------------------------------------------------------------===//

int main(int argc, char **argv) {
    ProgramArgs args;
    
    // 解析参数
    if (!parseArguments(argc, argv, args)) {
        return 1;
    }
    
    if (args.show_help) {
        printUsage(argv[0]);
        return 0;
    }
    
    if (args.verbose) {
        outs() << "Starting IRQ Analysis...\n";
        outs() << "Compile commands: " << args.compile_commands_path << "\n";
        outs() << "Handler definitions: " << args.handler_json_path << "\n";
        outs() << "Output file: " << args.output_path << "\n";
    }
    
    // 解析compile_commands.json
    CompileCommandsParser parser;
    if (!parser.parseFromFile(args.compile_commands_path)) {
        errs() << "Failed to parse compile_commands.json\n";
        return 1;
    }
    
    if (args.verbose) {
        outs() << "Found " << parser.getCommandCount() << " compile commands\n";
    }
    
    // 获取bitcode文件列表
    std::vector<std::string> bitcode_files = parser.getBitcodeFiles();
    
    if (bitcode_files.empty()) {
        errs() << "No bitcode files found. Make sure to compile with -emit-llvm\n";
        errs() << "Expected .bc files corresponding to source files in compile_commands.json\n";
        return 1;
    }
    
    if (args.verbose) {
        outs() << "Looking for " << bitcode_files.size() << " bitcode files\n";
    }
    
    LLVMContext Context;
    SMDiagnostic Err;
    
    size_t files_analyzed = 0;
    size_t files_skipped = 0;
    
    // 逐个分析bitcode文件
    for (const std::string& bc_file : bitcode_files) {
        if (args.verbose) {
            outs() << "Checking bitcode file: " << bc_file << "\n";
        }
        
        // 检查文件是否存在
        if (!sys::fs::exists(bc_file)) {
            if (args.verbose) {
                outs() << "Skipping non-existent file: " << bc_file << "\n";
            }
            files_skipped++;
            continue;
        }
        
        // 加载LLVM IR
        std::unique_ptr<Module> M = parseIRFile(bc_file, Err, Context);
        if (!M) {
            if (args.verbose) {
                errs() << "Error loading " << bc_file << ": " << Err.getMessage() << "\n";
            }
            files_skipped++;
            continue;
        }
        
        if (args.verbose) {
            outs() << "Successfully loaded: " << bc_file << "\n";
            outs() << "  Module name: " << M->getName().str() << "\n";
            outs() << "  Functions: " << M->size() << "\n";
        }
        
        // 创建pass manager
        legacy::PassManager PM;
        
        // 添加必需的分析pass
        PM.add(new CallGraphWrapperPass());
        
        // 添加我们的分析pass
        IRQAnalysisPass *analysis_pass = new IRQAnalysisPass(args.output_path, args.handler_json_path);
        PM.add(analysis_pass);
        
        // 运行pass
        PM.run(*M);
        
        files_analyzed++;
        
        if (args.show_stats) {
            outs() << "File: " << bc_file << " - Analysis completed\n";
        }
    }
    
    // 输出统计信息
    if (args.show_stats || args.verbose) {
        outs() << "\n=== Analysis Statistics ===\n";
        outs() << "Bitcode files found: " << bitcode_files.size() << "\n";
        outs() << "Files analyzed: " << files_analyzed << "\n";
        outs() << "Files skipped: " << files_skipped << "\n";
        outs() << "Results written to: " << args.output_path << "\n";
    }
    
    if (files_analyzed == 0) {
        errs() << "No files were successfully analyzed!\n";
        errs() << "\nTroubleshooting:\n";
        errs() << "1. Make sure .bc files exist (compile with -emit-llvm)\n";
        errs() << "2. Check that compile_commands.json contains valid paths\n";
        errs() << "3. Verify bitcode files are valid LLVM IR\n";
        errs() << "\nExample compilation:\n";
        errs() << "  clang -emit-llvm -c source.c -o source.bc\n";
        return 1;
    }
    
    outs() << "Analysis completed successfully.\n";
    outs() << "Check " << args.output_path << " for results.\n";
    return 0;
}
