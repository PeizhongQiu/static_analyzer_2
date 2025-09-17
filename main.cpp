//===- main.cpp - Main Program Entry Point ------------------------------===//
//
// 中断处理函数分析器主程序
//
//===----------------------------------------------------------------------===//

#include "CompileCommandsParser.h"
#include "IRQAnalysisPass.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Analysis/CallGraph.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// 命令行参数定义
//===----------------------------------------------------------------------===//

static cl::opt<std::string> CompileCommandsPath("compile-commands",
    cl::desc("Path to compile_commands.json file"),
    cl::value_desc("filename"),
    cl::Required);

static cl::opt<std::string> HandlerJsonPath("handlers",
    cl::desc("Path to handler.json file containing interrupt handler names"),
    cl::value_desc("filename"),
    cl::Required);

static cl::opt<std::string> OutputPath("output",
    cl::desc("Output file for analysis results"),
    cl::value_desc("filename"),
    cl::init("irq_analysis_results.json"));

static cl::opt<bool> Verbose("verbose",
    cl::desc("Enable verbose output"),
    cl::init(false));

static cl::opt<bool> ShowStatistics("stats",
    cl::desc("Show analysis statistics"),
    cl::init(false));

//===----------------------------------------------------------------------===//
// 主程序
//===----------------------------------------------------------------------===//

int main(int argc, char **argv) {
    cl::ParseCommandLineOptions(argc, argv, "LLVM Interrupt Handler Analyzer\n");
    
    if (Verbose) {
        outs() << "Starting IRQ Analysis...\n";
        outs() << "Compile commands: " << CompileCommandsPath << "\n";
        outs() << "Handler definitions: " << HandlerJsonPath << "\n";
        outs() << "Output file: " << OutputPath << "\n";
    }
    
    // 解析compile_commands.json
    CompileCommandsParser parser;
    if (!parser.parseFromFile(CompileCommandsPath)) {
        errs() << "Failed to parse compile_commands.json\n";
        return 1;
    }
    
    if (Verbose) {
        outs() << "Found " << parser.getCommandCount() << " compile commands\n";
    }
    
    // 获取bitcode文件列表
    std::vector<std::string> bitcode_files = parser.getBitcodeFiles();
    
    if (bitcode_files.empty()) {
        errs() << "No bitcode files found. Make sure to compile with -emit-llvm\n";
        return 1;
    }
    
    LLVMContext Context;
    SMDiagnostic Err;
    
    size_t total_handlers_found = 0;
    size_t files_analyzed = 0;
    size_t files_skipped = 0;
    
    // 逐个分析bitcode文件
    for (const std::string& bc_file : bitcode_files) {
        if (Verbose) {
            outs() << "Analyzing bitcode file: " << bc_file << "\n";
        }
        
        // 检查文件是否存在
        if (!sys::fs::exists(bc_file)) {
            if (Verbose) {
                outs() << "Skipping non-existent file: " << bc_file << "\n";
            }
            files_skipped++;
            continue;
        }
        
        // 加载LLVM IR
        std::unique_ptr<Module> M = parseIRFile(bc_file, Err, Context);
        if (!M) {
            errs() << "Error loading " << bc_file << ": " << Err.getMessage() << "\n";
            files_skipped++;
            continue;
        }
        
        // 运行分析pass
        legacy::PassManager PM;
        
        // 添加必需的分析pass
        PM.add(new CallGraphWrapperPass());
        
        // 添加我们的分析pass，传入handler.json路径
        IRQAnalysisPass *analysis_pass = new IRQAnalysisPass(OutputPath, HandlerJsonPath);
        PM.add(analysis_pass);
        
        // 运行pass
        PM.run(*M);
        
        files_analyzed++;
        
        if (ShowStatistics) {
            outs() << "File: " << bc_file << " - Functions analyzed\n";
        }
    }
    
    // 输出统计信息
    if (ShowStatistics || Verbose) {
        outs() << "\n=== Analysis Statistics ===\n";
        outs() << "Files found: " << bitcode_files.size() << "\n";
        outs() << "Files analyzed: " << files_analyzed << "\n";
        outs() << "Files skipped: " << files_skipped << "\n";
        outs() << "Results written to: " << OutputPath << "\n";
    }
    
    if (files_analyzed == 0) {
        errs() << "No files were successfully analyzed!\n";
        return 1;
    }
    
    outs() << "Analysis completed successfully.\n";
    return 0;
}
