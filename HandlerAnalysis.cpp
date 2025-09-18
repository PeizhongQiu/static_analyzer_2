//===- HandlerAnalysis.cpp - 中断处理函数分析实现 ------------------------===//

#include "CrossModuleAnalyzer.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

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
        
        // 显示增强分析的额外信息
        int cross_module_calls = 0;
        int static_accesses = 0;
        int global_accesses = 0;
        int dataflow_confirmed = 0;
        
        for (const auto& call : analysis.function_calls) {
            if (call.analysis_reason.find("cross_module") != std::string::npos) {
                cross_module_calls++;
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
        
        outs() << "  Cross-module calls: " << cross_module_calls << "\n";
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
    
    // 深度函数调用分析（包括函数指针深度解析）
    analysis.function_calls = analyzeHandlerFunctionCalls(F);
    
    // 分析所有间接调用
    std::vector<MemoryAccessInfo> indirect_impacts;
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (!CI->getCalledFunction()) {
                    // 深度分析间接调用
                    auto candidates = deep_fp_analyzer->analyzeDeep(CI->getCalledOperand());
                    
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

// 函数调用分析实现
std::vector<FunctionCallInfo> CrossModuleAnalyzer::analyzeHandlerFunctionCalls(Function* F) {
    std::vector<FunctionCallInfo> calls;
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (Function* callee = CI->getCalledFunction()) {
                    // 直接函数调用
                    FunctionCallInfo info;
                    info.callee_name = callee->getName().str();
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
                    
                    calls.push_back(info);
                } else {
                    // 间接函数调用 - 使用深度分析
                    auto candidates = deep_fp_analyzer->analyzeDeep(CI->getCalledOperand());
                    
                    for (const auto& candidate : candidates) {
                        FunctionCallInfo info;
                        info.callee_name = candidate.function->getName().str();
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
