//===- IRQAnalysisPass.cpp - Main IRQ Analysis Pass Implementation ------===//

#include "IRQAnalysisPass.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

char IRQAnalysisPass::ID = 0;

bool IRQAnalysisPass::runOnModule(Module &M) {
    outs() << "Running IRQ Analysis Pass on module: " << M.getName() << "\n";
    
    // 检查必需的参数
    if (handler_json_path.empty()) {
        errs() << "Error: No handler.json file specified\n";
        return false;
    }
    
    // 初始化所有分析器
    InterruptHandlerIdentifier identifier;
    MemoryAccessAnalyzer mem_analyzer(&M.getDataLayout());
    InlineAsmAnalyzer asm_analyzer;
    FunctionPointerAnalyzer fp_analyzer(&M, &M.getDataLayout());
    FunctionCallAnalyzer call_analyzer(&fp_analyzer);
    
    // 加载中断处理函数
    if (!identifier.loadHandlersFromJson(handler_json_path, M)) {
        errs() << "Error: Failed to load handlers from " << handler_json_path << "\n";
        return false;
    }
    
    // 显示去重统计信息
    if (identifier.hasDuplicates()) {
        outs() << "\nDeduplication summary:\n";
        outs() << "  Total entries in JSON: " << identifier.getTotalHandlerEntries() << "\n";
        outs() << "  Duplicate handlers removed: " << identifier.getDuplicateCount() << "\n";
        outs() << "  Unique handlers: " << identifier.getHandlerNames().size() << "\n\n";
    }
    
    // 检查是否找到处理函数
    if (identifier.getHandlerCount() == 0) {
        outs() << "No interrupt handlers found in module\n";
        return false;
    }
    
    outs() << "Found " << identifier.getHandlerCount() << " interrupt handlers\n\n";
    
    // 分析所有识别出的处理函数
    std::vector<InterruptHandlerAnalysis> results;
    for (Function *F : identifier.getIdentifiedHandlers()) {
        outs() << "Analyzing handler: " << F->getName() << "\n";
        
        InterruptHandlerAnalysis analysis = analyzeSingleHandler(F, mem_analyzer, 
                                                               call_analyzer, 
                                                               fp_analyzer, 
                                                               asm_analyzer);
        results.push_back(analysis);
        outs() << "\n";
    }
    
    // 输出分析结果
    JSONOutputGenerator json_generator;
    json_generator.outputAnalysisResults(results, output_path);
    
    outs() << "Analysis completed. Results written to: " << output_path << "\n";
    return false;
}

InterruptHandlerAnalysis IRQAnalysisPass::analyzeSingleHandler(Function *F,
                                                             MemoryAccessAnalyzer &mem_analyzer,
                                                             FunctionCallAnalyzer &call_analyzer,
                                                             FunctionPointerAnalyzer &fp_analyzer,
                                                             InlineAsmAnalyzer &asm_analyzer) {
    InterruptHandlerAnalysis analysis;
    
    // 基本信息
    analysis.function_name = F->getName().str();
    analysis.is_confirmed_irq_handler = true;
    analysis.basic_block_count = F->size();
    
    // 源码位置信息
    if (auto *SP = F->getSubprogram()) {
        analysis.source_file = SP->getFilename().str();
        analysis.line_number = SP->getLine();
    }
    
    // 简单的循环统计
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
    
    // 1. 内存访问分析
    outs() << "  Analyzing memory accesses...\n";
    analysis.memory_accesses = mem_analyzer.analyzeFunction(*F);
    
    // 2. 函数调用分析
    outs() << "  Analyzing function calls...\n";
    analysis.function_calls = call_analyzer.analyzeFunctionCalls(*F);
    
    // 3. 间接调用的内存影响分析
    outs() << "  Analyzing indirect call impacts...\n";
    std::vector<MemoryAccessInfo> indirect_impacts = 
        call_analyzer.getIndirectCallMemoryImpacts(*F);
    
    // 合并直接和间接内存访问
    analysis.total_memory_accesses = analysis.memory_accesses;
    analysis.total_memory_accesses.insert(analysis.total_memory_accesses.end(),
                                        indirect_impacts.begin(), indirect_impacts.end());
    
    // 4. 详细的间接调用分析
    outs() << "  Analyzing indirect calls in detail...\n";
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
    
    // 5. 内联汇编分析
    outs() << "  Analyzing inline assembly...\n";
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
    
    // 6. 符号统计
    outs() << "  Building accessed symbols summary...\n";
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
    
    // 7. 递归调用检测
    analysis.has_recursive_calls = detectRecursiveCalls(*F);
    
    // 输出统计信息
    outs() << "    Memory accesses: " << analysis.memory_accesses.size() << "\n";
    outs() << "    Total memory accesses (including indirect): " << analysis.total_memory_accesses.size() << "\n";
    outs() << "    Function calls: " << analysis.function_calls.size() << "\n";
    outs() << "    Indirect calls: " << analysis.indirect_call_analyses.size() << "\n";
    outs() << "    Register accesses: " << analysis.register_accesses.size() << "\n";
    outs() << "    Accessed global vars: " << analysis.accessed_global_vars.size() << "\n";
    outs() << "    Accessed struct types: " << analysis.accessed_struct_types.size() << "\n";
    
    // 显示一些具体的访问信息
    if (!analysis.accessed_global_vars.empty()) {
        outs() << "    Key global variables: ";
        int count = 0;
        for (const auto& var : analysis.accessed_global_vars) {
            if (count++ > 0) outs() << ", ";
            outs() << var;
            if (count >= 5) { // 只显示前5个
                if (analysis.accessed_global_vars.size() > 5) {
                    outs() << " (+" << (analysis.accessed_global_vars.size() - 5) << " more)";
                }
                break;
            }
        }
        outs() << "\n";
    }
    
    // 显示高置信度的内存访问
    int high_confidence_accesses = 0;
    int device_related_accesses = 0;
    for (const auto& access : analysis.total_memory_accesses) {
        if (access.confidence >= 80) high_confidence_accesses++;
        if (access.isDeviceRelatedAccess()) device_related_accesses++;
    }
    
    if (high_confidence_accesses > 0) {
        outs() << "    High confidence accesses: " << high_confidence_accesses << "\n";
    }
    if (device_related_accesses > 0) {
        outs() << "    Device-related accesses: " << device_related_accesses << "\n";
    }
    
    return analysis;
}

bool IRQAnalysisPass::detectRecursiveCalls(Function &F) {
    std::set<Function*> visited;
    std::set<Function*> in_path;
    return detectRecursiveCallsHelper(&F, visited, in_path);
}

bool IRQAnalysisPass::detectRecursiveCallsHelper(Function *F, 
                                                std::set<Function*> &visited,
                                                std::set<Function*> &in_path) {
    if (in_path.find(F) != in_path.end()) {
        return true; // 发现循环
    }
    
    if (visited.find(F) != visited.end()) {
        return false; // 已经访问过
    }
    
    visited.insert(F);
    in_path.insert(F);
    
    // 检查所有直接调用
    for (auto &BB : *F) {
        for (auto &I : BB) {
            if (auto *CI = dyn_cast<CallInst>(&I)) {
                if (Function *callee = CI->getCalledFunction()) {
                    if (detectRecursiveCallsHelper(callee, visited, in_path)) {
                        return true;
                    }
                }
            }
        }
    }
    
    in_path.erase(F);
    return false;
}

void IRQAnalysisPass::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<CallGraphWrapperPass>();
    AU.setPreservesAll();
}

// Pass注册
static RegisterPass<IRQAnalysisPass> X("irq-analysis", 
                                       "Interrupt Handler Analysis Pass",
                                       false, false);
