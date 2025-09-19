//===- HandlerAnalysis.cpp - 中断处理函数分析实现 ------------------------===//

#include "CrossModuleAnalyzer.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// 辅助函数：检查是否为真正的间接调用
//===----------------------------------------------------------------------===//

static bool isActualIndirectCall(CallInst* CI) {
    if (CI->getCalledFunction()) {
        return false; // 有直接目标，不是间接调用
    }
    
    Value* callee = CI->getCalledOperand();
    
    // 检查是否是内联汇编
    if (isa<InlineAsm>(callee)) {
        errs() << "DEBUG: Skipping inline assembly call\n";
        return false;
    }
    
    // 检查是否是常量表达式（可能是直接调用的变形）
    if (isa<ConstantExpr>(callee)) {
        errs() << "DEBUG: Skipping constant expression call: " << *callee << "\n";
        return false;
    }
    
    // 检查是否是直接的函数指针常量
    if (isa<Function>(callee)) {
        errs() << "DEBUG: Direct function reference, not indirect: " << callee->getName() << "\n";
        return false;
    }
    
    // 只有通过寄存器或内存加载的函数指针才是真正的间接调用
    bool isIndirect = isa<LoadInst>(callee) || isa<Argument>(callee) || isa<PHINode>(callee);
    
    if (isIndirect) {
        errs() << "DEBUG: Found actual indirect call, operand: " << *callee << "\n";
        errs() << "DEBUG: Operand type: " << *callee->getType() << "\n";
    } else {
        errs() << "DEBUG: Not a true indirect call, operand: " << *callee << "\n";
        errs() << "DEBUG: Operand type: " << *callee->getType() << "\n";
    }
    
    return isIndirect;
}

//===----------------------------------------------------------------------===//
// 辅助函数：检查是否为LLVM内置函数
//===----------------------------------------------------------------------===//

static bool isLLVMIntrinsicFunction(const std::string& name) {
    // LLVM内置函数前缀
    if (name.find("llvm.") == 0) {
        return true;
    }
    
    // 编译器插桩函数
    static const std::vector<std::string> instrumentation_prefixes = {
        "__sanitizer_cov_",
        "__asan_",
        "__msan_",
        "__tsan_",
        "__ubsan_",
        "__gcov_",
        "__llvm_gcov_",
        "__llvm_gcda_",
        "__llvm_gcno_",
        "__coverage_",
        "__profile_"
    };
    
    for (const auto& prefix : instrumentation_prefixes) {
        if (name.find(prefix) == 0) {
            return true;
        }
    }
    
    return false;
}

static bool isCompilerGeneratedFunction(const std::string& name) {
    // 检查是否是编译器生成的函数
    static const std::vector<std::string> compiler_generated = {
        "__stack_chk_fail",
        "__stack_chk_guard", 
        "_GLOBAL__sub_I_",
        "__cxx_global_var_init",
        "__dso_handle",
        "_ZN", // C++ mangled names (可选择过滤)
    };
    
    for (const auto& pattern : compiler_generated) {
        if (name.find(pattern) == 0) {
            return true;
        }
    }
    
    return false;
}

static bool shouldFilterFunction(const std::string& name) {
    return isLLVMIntrinsicFunction(name) || isCompilerGeneratedFunction(name);
}

//===----------------------------------------------------------------------===//
// 中断处理函数分析实现
//===----------------------------------------------------------------------===//

std::vector<InterruptHandlerAnalysis> CrossModuleAnalyzer::analyzeAllHandlers(const std::string& handler_json) {
    std::vector<InterruptHandlerAnalysis> all_results;
    
    outs() << "Starting enhanced cross-module handler analysis...\n";
    
    // 在所有模块中查找处理函数
    InterruptHandlerIdentifier identifier;
    std::set<Function*> all_handlers;
    
    for (auto& M : modules) {
        if (identifier.loadHandlersFromJson(handler_json, *M)) {
            const auto& handlers = identifier.getIdentifiedHandlers();
            all_handlers.insert(handlers.begin(), handlers.end());
        }
    }
    
    if (all_handlers.empty()) {
        outs() << "No interrupt handlers found in any module\n";
        return all_results;
    }
    
    outs() << "Found " << all_handlers.size() << " interrupt handlers across all modules:\n";
    for (Function* F : all_handlers) {
        Module* M = enhanced_symbols.function_to_module[F];
        SymbolScope scope = getFunctionScope(F);
        std::string scope_str = (scope == SymbolScope::GLOBAL) ? "global" : 
                              (scope == SymbolScope::STATIC) ? "static" : "other";
        outs() << "  - " << F->getName() << " (" << scope_str << " in " << M->getName() << ")\n";
    }
    outs() << "\n";
    
    // 深度分析每个处理函数
    for (Function* F : all_handlers) {
        outs() << "Deep analyzing handler: " << F->getName() << "\n";
        
        InterruptHandlerAnalysis analysis = analyzeHandlerDeep(F);
        all_results.push_back(analysis);
        
        // 显示增强分析的额外信息（过滤无意义的调用）
        int meaningful_calls = 0;
        int static_accesses = 0;
        int global_accesses = 0;
        int dataflow_confirmed = 0;
        
        for (const auto& call : analysis.function_calls) {
            if (!shouldFilterFunction(call.callee_name)) {
                meaningful_calls++;
            }
        }
        
        for (const auto& access : analysis.total_memory_accesses) {
            if (access.chain_description.find("static") != std::string::npos) {
                static_accesses++;
            }
            if (access.chain_description.find("global") != std::string::npos) {
                global_accesses++;
            }
            if (access.chain_description.find("dataflow_confirmed") != std::string::npos) {
                dataflow_confirmed++;
            }
        }
        
        outs() << "  Meaningful function calls: " << meaningful_calls << " (filtered out " 
               << (analysis.function_calls.size() - meaningful_calls) << " intrinsics)\n";
        outs() << "  Static variable accesses: " << static_accesses << "\n";
        outs() << "  Global variable accesses: " << global_accesses << "\n";
        outs() << "  Dataflow confirmed accesses: " << dataflow_confirmed << "\n";
        outs() << "  Total memory accesses: " << analysis.total_memory_accesses.size() << "\n";
        outs() << "\n";
    }
    
    return all_results;
}

InterruptHandlerAnalysis CrossModuleAnalyzer::analyzeHandlerDeep(Function* F) {
    InterruptHandlerAnalysis analysis;
    
    // 基本信息
    analysis.function_name = F->getName().str();
    analysis.is_confirmed_irq_handler = true;
    analysis.basic_block_count = F->size();
    
    // 获取所属模块和作用域信息
    Module* owner_module = enhanced_symbols.function_to_module[F];
    SymbolScope scope = getFunctionScope(F);
    
    if (owner_module) {
        analysis.source_file = owner_module->getName().str();
    }
    
    // 添加作用域信息到分析结果
    std::string scope_info = "scope:";
    switch (scope) {
        case SymbolScope::GLOBAL: scope_info += "global"; break;
        case SymbolScope::STATIC: scope_info += "static"; break;
        case SymbolScope::WEAK: scope_info += "weak"; break;
        default: scope_info += "other"; break;
    }
    analysis.source_file += " (" + scope_info + ")";
    
    // 源码位置信息
    if (auto* SP = F->getSubprogram()) {
        analysis.source_file = SP->getFilename().str();
        analysis.line_number = SP->getLine();
    }
    
    // 控制流统计
    analysis.loop_count = 0;
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* BI = dyn_cast<BranchInst>(&I)) {
                if (BI->isConditional()) {
                    analysis.loop_count++;
                }
            }
        }
    }
    
    // 使用数据流增强的内存访问分析
    EnhancedCrossModuleMemoryAnalyzer* enhanced_mem_analyzer = 
        static_cast<EnhancedCrossModuleMemoryAnalyzer*>(memory_analyzer.get());
    analysis.memory_accesses = enhanced_mem_analyzer->analyzeWithDataFlow(*F);
    
    // 深度函数调用分析（包括函数指针深度解析）- 带过滤和调试
    analysis.function_calls = analyzeHandlerFunctionCalls(F);
    
    // 分析所有间接调用 - 修复版本
    std::vector<MemoryAccessInfo> indirect_impacts;
    int potential_indirect_calls = 0;
    int actual_indirect_calls = 0;
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (!CI->getCalledFunction()) {
                    potential_indirect_calls++;
                    
                    // 添加详细调试信息
                    errs() << "DEBUG: Potential indirect call in " << F->getName() << "\n";
                    errs() << "DEBUG: Call instruction: " << *CI << "\n";
                    errs() << "DEBUG: Called operand: " << *CI->getCalledOperand() << "\n";
                    errs() << "DEBUG: Operand type: " << *CI->getCalledOperand()->getType() << "\n";
                    
                    // 检查是否是真正的间接调用
                    if (!isActualIndirectCall(CI)) {
                        errs() << "DEBUG: Skipping non-indirect call\n";
                        continue;
                    }
                    
                    actual_indirect_calls++;
                    
                    // 深度分析间接调用
                    auto candidates = deep_fp_analyzer->analyzeDeep(CI->getCalledOperand());
                    
                    errs() << "DEBUG: Found " << candidates.size() << " candidates for indirect call\n";
                    
                    for (const auto& candidate : candidates) {
                        if (candidate.requires_further_analysis && candidate.confidence >= 60) {
                            // 分析候选函数的内存访问
                            InterruptHandlerAnalysis candidate_analysis = 
                                deep_fp_analyzer->analyzeCandidateFunction(candidate.function);
                            
                            // 合并内存访问（降低置信度）
                            for (auto access : candidate_analysis.total_memory_accesses) {
                                access.confidence = (access.confidence * candidate.confidence) / 100;
                                access.chain_description += " (via_indirect_call:" + candidate.match_reason + ")";
                                indirect_impacts.push_back(access);
                            }
                        }
                    }
                    
                    // 记录间接调用信息
                    IndirectCallAnalysis indirect_analysis;
                    indirect_analysis.call_inst = CI;
                    
                    // 转换候选函数为标准格式
                    for (const auto& candidate : candidates) {
                        FunctionPointerTarget target(candidate.function, candidate.confidence, candidate.match_reason);
                        indirect_analysis.fp_analysis.possible_targets.push_back(target);
                    }
                    
                    indirect_analysis.aggregated_accesses = indirect_impacts;
                    analysis.indirect_call_analyses.push_back(indirect_analysis);
                }
            }
        }
    }
    
    // 输出调试统计信息
    errs() << "DEBUG: Function " << F->getName() << " analysis summary:\n";
    errs() << "DEBUG: Potential indirect calls detected: " << potential_indirect_calls << "\n";
    errs() << "DEBUG: Actual indirect calls confirmed: " << actual_indirect_calls << "\n";
    errs() << "DEBUG: Indirect call analyses created: " << analysis.indirect_call_analyses.size() << "\n";
    
    // 合并直接和间接内存访问
    analysis.total_memory_accesses = analysis.memory_accesses;
    analysis.total_memory_accesses.insert(analysis.total_memory_accesses.end(),
                                        indirect_impacts.begin(), indirect_impacts.end());
    
    // 内联汇编分析
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (auto* IA = dyn_cast<InlineAsm>(CI->getCalledOperand())) {
                    auto reg_accesses = asm_analyzer->analyzeInlineAsm(IA);
                    analysis.register_accesses.insert(
                        analysis.register_accesses.end(),
                        reg_accesses.begin(), reg_accesses.end());
                }
            }
        }
    }
    
    // 符号统计（区分static和global）
    for (const auto& access : analysis.total_memory_accesses) {
        if (access.type == MemoryAccessInfo::GLOBAL_VARIABLE) {
            analysis.accessed_global_vars.insert(access.symbol_name);
        } else if (access.type == MemoryAccessInfo::STRUCT_FIELD_ACCESS ||
                  access.type == MemoryAccessInfo::POINTER_CHAIN_ACCESS) {
            if (!access.struct_type_name.empty()) {
                analysis.accessed_struct_types.insert(access.struct_type_name);
            }
            for (const auto& elem : access.pointer_chain.elements) {
                if (!elem.struct_type_name.empty()) {
                    analysis.accessed_struct_types.insert(elem.struct_type_name);
                }
            }
        }
    }
    
    // 递归调用检测（简化）
    analysis.has_recursive_calls = false;
    
    return analysis;
}

// 函数调用分析实现 - 带过滤和调试
std::vector<FunctionCallInfo> CrossModuleAnalyzer::analyzeHandlerFunctionCalls(Function* F) {
    std::vector<FunctionCallInfo> calls;
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (Function* callee = CI->getCalledFunction()) {
                    // 直接函数调用 - 过滤LLVM内置函数
                    std::string callee_name = callee->getName().str();
                    
                    errs() << "DEBUG: Direct call to: " << callee_name << "\n";
                    
                    // 跳过LLVM内置函数和编译器生成的函数
                    if (shouldFilterFunction(callee_name)) {
                        errs() << "DEBUG: Filtering out intrinsic/compiler function: " << callee_name << "\n";
                        continue;
                    }
                    
                    FunctionCallInfo info;
                    info.callee_name = callee_name;
                    info.is_direct_call = true;
                    info.confidence = 100;
                    
                    // 检查是否为跨模块调用
                    Module* caller_module = enhanced_symbols.function_to_module[F];
                    Module* callee_module = enhanced_symbols.function_to_module[callee];
                    
                    if (caller_module != callee_module) {
                        info.analysis_reason = "cross_module_direct_call";
                    } else {
                        info.analysis_reason = "same_module_direct_call";
                    }
                    
                    // 检查作用域
                    SymbolScope callee_scope = getFunctionScope(callee);
                    if (callee_scope == SymbolScope::STATIC) {
                        info.analysis_reason += "_static_function";
                    } else if (callee_scope == SymbolScope::GLOBAL) {
                        info.analysis_reason += "_global_function";
                    }
                    
                    // 判断是否为内核函数（简单启发式）
                    if (callee_name.find("pci_") == 0 || 
                        callee_name.find("kmalloc") != std::string::npos ||
                        callee_name.find("printk") != std::string::npos ||
                        callee_name.find("spin_") == 0 ||
                        callee_name.find("mutex_") == 0) {
                        info.is_kernel_function = true;
                    }
                    
                    calls.push_back(info);
                } else {
                    // 潜在的间接函数调用 - 需要验证
                    errs() << "DEBUG: Checking potential indirect call\n";
                    
                    if (!isActualIndirectCall(CI)) {
                        errs() << "DEBUG: Not an actual indirect call, skipping function pointer analysis\n";
                        continue;
                    }
                    
                    // 使用深度分析
                    auto candidates = deep_fp_analyzer->analyzeDeep(CI->getCalledOperand());
                    
                    for (const auto& candidate : candidates) {
                        std::string candidate_name = candidate.function->getName().str();
                        
                        errs() << "DEBUG: Indirect call candidate: " << candidate_name << " (confidence: " << candidate.confidence << ")\n";
                        
                        // 跳过LLVM内置函数
                        if (shouldFilterFunction(candidate_name)) {
                            errs() << "DEBUG: Filtering out intrinsic candidate: " << candidate_name << "\n";
                            continue;
                        }
                        
                        FunctionCallInfo info;
                        info.callee_name = candidate_name;
                        info.is_direct_call = false;
                        info.confidence = candidate.confidence;
                        info.analysis_reason = candidate.match_reason;
                        
                        if (candidate.scope == SymbolScope::STATIC) {
                            info.analysis_reason += "_static_target";
                        } else if (candidate.scope == SymbolScope::GLOBAL) {
                            info.analysis_reason += "_global_target";
                        }
                        
                        calls.push_back(info);
                    }
                }
            }
        }
    }
    
    return calls;
}
