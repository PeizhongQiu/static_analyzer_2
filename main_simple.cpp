//===- main_simple.cpp - Fixed Main Program for Multiple Handlers ------===//
//
// 简化版主程序，避免LLVM命令行选项冲突，正确收集所有处理函数
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
#include "llvm/Support/JSON.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <string>
#include <cstring>
#include <vector>

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
    
    // *** 关键修改：收集所有分析结果 ***
    std::vector<InterruptHandlerAnalysis> all_results;
    
    // 逐个分析bitcode文件，但收集所有结果
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
        
        // *** 关键修改：自定义分析逻辑而不是使用Pass ***
        
        // 初始化分析器
        InterruptHandlerIdentifier identifier;
        MemoryAccessAnalyzer mem_analyzer(&M->getDataLayout());
        InlineAsmAnalyzer asm_analyzer;
        FunctionPointerAnalyzer fp_analyzer(M.get(), &M->getDataLayout());
        FunctionCallAnalyzer call_analyzer(&fp_analyzer);
        
        // 加载中断处理函数
        if (!identifier.loadHandlersFromJson(args.handler_json_path, *M)) {
            if (args.verbose) {
                outs() << "No handlers found in " << bc_file << "\n";
            }
            continue;
        }
        
        // 如果找到了处理函数，进行分析
        if (identifier.getHandlerCount() > 0) {
            if (args.verbose) {
                outs() << "Found " << identifier.getHandlerCount() 
                       << " handlers in " << bc_file << ":\n";
                for (Function *F : identifier.getIdentifiedHandlers()) {
                    outs() << "  - " << F->getName() << "\n";
                }
            }
            
            // 分析每个找到的处理函数
            for (Function *F : identifier.getIdentifiedHandlers()) {
                if (args.verbose) {
                    outs() << "Analyzing handler: " << F->getName() << "\n";
                }
                
                InterruptHandlerAnalysis analysis;
                
                // 基本信息
                analysis.function_name = F->getName().str();
                analysis.is_confirmed_irq_handler = true;
                analysis.basic_block_count = F->size();
                
                // 源码位置信息（简化处理避免编译错误）
                analysis.source_file = bc_file; // 使用bitcode文件路径
                analysis.line_number = 0;
                
                // 循环统计
                analysis.loop_count = 0;
                for (auto &BB : *F) {
                    for (auto &I : BB) {
                        if (auto *BI = dyn_cast<BranchInst>(&I)) {
                            if (BI->isConditional()) {
                                analysis.loop_count++;
                            }
                        }
                    }
                }
                
                // 内存访问分析 - 只记录写操作
                std::vector<MemoryAccessInfo> all_accesses = mem_analyzer.analyzeFunction(*F);
                for (const auto& access : all_accesses) {
                    if (access.is_write) {  // 只保留写操作
                        analysis.memory_accesses.push_back(access);
                    }
                }
                
                // 函数调用分析
                analysis.function_calls = call_analyzer.analyzeFunctionCalls(*F);
                
                // 间接调用的内存影响分析 - 只记录写操作
                std::vector<MemoryAccessInfo> all_indirect_impacts = 
                    call_analyzer.getIndirectCallMemoryImpacts(*F);
                
                std::vector<MemoryAccessInfo> indirect_impacts;
                for (const auto& access : all_indirect_impacts) {
                    if (access.is_write) {  // 只保留写操作
                        indirect_impacts.push_back(access);
                    }
                }
                
                // 合并直接和间接内存访问（都是写操作）
                analysis.total_memory_accesses = analysis.memory_accesses;
                analysis.total_memory_accesses.insert(analysis.total_memory_accesses.end(),
                                                    indirect_impacts.begin(), indirect_impacts.end());
                
                // 间接调用分析
                for (auto &BB : *F) {
                    for (auto &I : BB) {
                        if (auto *CI = dyn_cast<CallInst>(&I)) {
                            if (!CI->getCalledFunction()) {
                                IndirectCallAnalysis indirect_analysis = 
                                    fp_analyzer.analyzeIndirectCall(CI);
                                analysis.indirect_call_analyses.push_back(indirect_analysis);
                            }
                        }
                    }
                }
                
                // 内联汇编分析
                for (auto &BB : *F) {
                    for (auto &I : BB) {
                        if (auto *CI = dyn_cast<CallInst>(&I)) {
                            if (auto *IA = dyn_cast<InlineAsm>(CI->getCalledOperand())) {
                                auto reg_accesses = asm_analyzer.analyzeInlineAsm(IA);
                                analysis.register_accesses.insert(
                                    analysis.register_accesses.end(),
                                    reg_accesses.begin(), reg_accesses.end());
                            }
                        }
                    }
                }
                
                // 符号统计
                for (const auto &access : analysis.total_memory_accesses) {
                    if (access.type == MemoryAccessInfo::GLOBAL_VARIABLE) {
                        analysis.accessed_global_vars.insert(access.symbol_name);
                    } else if (access.type == MemoryAccessInfo::STRUCT_FIELD_ACCESS ||
                              access.type == MemoryAccessInfo::POINTER_CHAIN_ACCESS) {
                        if (!access.struct_type_name.empty()) {
                            analysis.accessed_struct_types.insert(access.struct_type_name);
                        }
                        for (const auto &elem : access.pointer_chain.elements) {
                            if (!elem.struct_type_name.empty()) {
                                analysis.accessed_struct_types.insert(elem.struct_type_name);
                            }
                        }
                    }
                }
                
                // 递归调用检测（简化版）
                analysis.has_recursive_calls = false; // 简化处理
                
                // *** 关键：将结果添加到总列表中 ***
                all_results.push_back(analysis);
                
                if (args.verbose) {
                    outs() << "  Memory accesses: " << analysis.memory_accesses.size() << "\n";
                    outs() << "  Function calls: " << analysis.function_calls.size() << "\n";
                    outs() << "  High confidence accesses: ";
                    int high_conf = 0;
                    for (const auto& access : analysis.total_memory_accesses) {
                        if (access.confidence >= 80) high_conf++;
                    }
                    outs() << high_conf << "\n";
                }
            }
        }
        
        files_analyzed++;
    }
    
    // *** 关键修改：一次性输出所有结果 ***
    if (!all_results.empty()) {
        JSONOutputGenerator json_generator;
        json_generator.outputAnalysisResults(all_results, args.output_path);
        
        outs() << "\n=== Final Analysis Summary ===\n";
        outs() << "Total handlers found and analyzed: " << all_results.size() << "\n";
        outs() << "Handlers:\n";
        for (const auto& analysis : all_results) {
            outs() << "  ✓ " << analysis.function_name 
                   << " (" << analysis.total_memory_accesses.size() << " memory accesses, "
                   << analysis.function_calls.size() << " function calls)\n";
        }
        outs() << "Results written to: " << args.output_path << "\n";
    } else {
        outs() << "No interrupt handlers found in any bitcode files!\n";
        return 1;
    }
    
    // 输出统计信息
    if (args.show_stats || args.verbose) {
        outs() << "\n=== Analysis Statistics ===\n";
        outs() << "Bitcode files found: " << bitcode_files.size() << "\n";
        outs() << "Files analyzed: " << files_analyzed << "\n";
        outs() << "Files skipped: " << files_skipped << "\n";
        outs() << "Total handlers found: " << all_results.size() << "\n";
    }
    
    outs() << "Analysis completed successfully.\n";
    return 0;
}
