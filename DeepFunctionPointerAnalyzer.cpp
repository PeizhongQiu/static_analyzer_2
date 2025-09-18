//===- DeepFunctionPointerAnalyzer.cpp - 深度函数指针分析器实现 -----------===//

#include "CrossModuleAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// DeepFunctionPointerAnalyzer 实现
//===----------------------------------------------------------------------===//

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeDeep(Value* fp_value) {
    std::vector<FunctionPointerCandidate> candidates;
    
    // 1. 基于类型的候选函数查找
    if (auto* ptr_type = dyn_cast<PointerType>(fp_value->getType())) {
        if (auto* func_type = dyn_cast<FunctionType>(ptr_type->getElementType())) {
            auto type_candidates = findCandidatesByType(func_type);
            candidates.insert(candidates.end(), type_candidates.begin(), type_candidates.end());
        }
    }
    
    // 2. 基于存储位置的分析
    auto storage_candidates = analyzeFunctionPointerStorage(fp_value);
    candidates.insert(candidates.end(), storage_candidates.begin(), storage_candidates.end());
    
    // 3. 如果是GEP指令，分析结构体字段
    if (auto* GEP = dyn_cast<GetElementPtrInst>(fp_value)) {
        auto struct_candidates = analyzeStructFunctionPointers(GEP);
        candidates.insert(candidates.end(), struct_candidates.begin(), struct_candidates.end());
    }
    
    // 4. 如果是全局变量，分析函数指针表
    if (auto* GV = dyn_cast<GlobalVariable>(fp_value)) {
        auto table_candidates = analyzeGlobalFunctionTable(GV);
        candidates.insert(candidates.end(), table_candidates.begin(), table_candidates.end());
    }
    
    // 5. 数据流分析
    DataFlowNode flow_info = dataflow_analyzer->getDataFlowInfo(fp_value);
    if (flow_info.node_type == "global" || flow_info.node_type == "static") {
        // 根据数据流信息调整候选函数的置信度
        for (auto& candidate : candidates) {
            if (flow_info.source_module && 
                global_symbols->function_to_module[candidate.function] == flow_info.source_module) {
                candidate.confidence += 15; // 同模块的函数获得额外置信度
            }
        }
    }
    
    // 去重并排序
    std::sort(candidates.begin(), candidates.end(), 
              [](const FunctionPointerCandidate& a, const FunctionPointerCandidate& b) {
                  return a.confidence > b.confidence;
              });
    
    // 移除重复项
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                [](const FunctionPointerCandidate& a, const FunctionPointerCandidate& b) {
                                    return a.function == b.function;
                                }), candidates.end());
    
    // 标记需要进一步分析的候选函数
    for (auto& candidate : candidates) {
        if (candidate.confidence >= 60 && 
            analyzed_functions.find(candidate.function) == analyzed_functions.end()) {
            candidate.requires_further_analysis = true;
        }
    }
    
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::findCandidatesByType(FunctionType* FT) {
    std::vector<FunctionPointerCandidate> candidates;
    
    // 构建函数类型签名
    std::string signature;
    signature += std::to_string(FT->getReturnType()->getTypeID()) + "_";
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
        signature += std::to_string(FT->getParamType(i)->getTypeID()) + "_";
    }
    
    // 查找匹配的函数
    auto it = global_symbols->signature_to_functions.find(signature);
    if (it != global_symbols->signature_to_functions.end()) {
        for (Function* F : it->second) {
            std::string reason = "signature_match";
            int confidence = 50;
            
            // 根据函数名调整置信度
            StringRef name = F->getName();
            if (name.contains("callback") || name.contains("handler") || 
                name.contains("interrupt") || name.contains("irq")) {
                confidence += 20;
                reason += "_callback_pattern";
            }
            
            if (name.endswith("_fn") || name.endswith("_func")) {
                confidence += 10;
                reason += "_function_suffix";
            }
            
            // 获取函数作用域
            SymbolScope scope = SymbolScope::GLOBAL;
            auto global_it = global_symbols->global_functions.find(F->getName().str());
            if (global_it == global_symbols->global_functions.end()) {
                scope = SymbolScope::STATIC;
                reason += "_static_function";
            }
            
            std::string module_name = global_symbols->function_to_module[F]->getName().str();
            candidates.emplace_back(F, confidence, reason, module_name, scope);
        }
    }
    
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeFunctionPointerStorage(Value* fp_value) {
    std::vector<FunctionPointerCandidate> candidates;
    
    // 查找对这个值的所有存储操作
    for (auto* User : fp_value->users()) {
        if (auto* SI = dyn_cast<StoreInst>(User)) {
            Value* stored_value = SI->getValueOperand();
            
            if (auto* F = dyn_cast<Function>(stored_value)) {
                std::string reason = "stored_function_pointer";
                int confidence = 75;
                
                // 检查存储位置的数据流信息
                DataFlowNode store_location = dataflow_analyzer->getDataFlowInfo(SI->getPointerOperand());
                if (store_location.node_type == "global") {
                    confidence += 10;
                    reason += "_global_storage";
                } else if (store_location.node_type == "static") {
                    confidence += 5;
                    reason += "_static_storage";
                }
                
                SymbolScope scope = global_symbols->global_functions.find(F->getName().str()) != 
                                   global_symbols->global_functions.end() ? 
                                   SymbolScope::GLOBAL : SymbolScope::STATIC;
                
                std::string module_name = global_symbols->function_to_module[F]->getName().str();
                candidates.emplace_back(F, confidence, reason, module_name, scope);
            }
        }
    }
    
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeStructFunctionPointers(GetElementPtrInst* GEP) {
    std::vector<FunctionPointerCandidate> candidates;
    
    Type* source_type = GEP->getSourceElementType();
    if (auto* struct_type = dyn_cast<StructType>(source_type)) {
        std::string struct_name = struct_type->getName().str();
        
        // 分析这个结构体类型在所有模块中的使用
        for (auto& M : global_symbols->module_by_name) {
            Module* module = M.second;
            
            // 查找对这个结构体字段的赋值
            for (auto& F : *module) {
                for (auto& BB : F) {
                    for (auto& I : BB) {
                        if (auto* SI = dyn_cast<StoreInst>(&I)) {
                            Value* ptr = SI->getPointerOperand();
                            if (auto* other_GEP = dyn_cast<GetElementPtrInst>(ptr)) {
                                // 检查是否是同一个结构体字段
                                if (other_GEP->getSourceElementType() == struct_type &&
                                    GEP->getNumOperands() == other_GEP->getNumOperands()) {
                                    
                                    bool same_field = true;
                                    for (unsigned i = 1; i < GEP->getNumOperands(); ++i) {
                                        if (GEP->getOperand(i) != other_GEP->getOperand(i)) {
                                            same_field = false;
                                            break;
                                        }
                                    }
                                    
                                    if (same_field) {
                                        Value* stored_func = SI->getValueOperand();
                                        if (auto* func = dyn_cast<Function>(stored_func)) {
                                            std::string reason = "struct_field_assignment:" + struct_name;
                                            int confidence = 80;
                                            
                                            SymbolScope scope = global_symbols->global_functions.find(func->getName().str()) != 
                                                               global_symbols->global_functions.end() ? 
                                                               SymbolScope::GLOBAL : SymbolScope::STATIC;
                                            
                                            std::string module_name = global_symbols->function_to_module[func]->getName().str();
                                            candidates.emplace_back(func, confidence, reason, module_name, scope);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
        [O    }
        }
    }
    
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeGlobalFunctionTable(GlobalVariable* GV) {
    std::vector<FunctionPointerCandidate> candidates;
    
    if (GV->hasInitializer()) {
        Constant* init = GV->getInitializer();
        
        // 分析数组初始化器
        if (auto* CA = dyn_cast<ConstantArray>(init)) {
            for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
                Value* element = CA->getOperand(i);
                
                if (auto* func = dyn_cast<Function>(element)) {
                    std::string reason = "global_function_table:" + GV->getName().str();
                    int confidence = 85;
                    
                    SymbolScope scope = global_symbols->global_functions.find(func->getName().str()) != 
                                       global_symbols->global_functions.end() ? 
                                       SymbolScope::GLOBAL : SymbolScope::STATIC;
                    
                    std::string module_name = global_symbols->function_to_module[func]->getName().str();
                    candidates.emplace_back(func, confidence, reason, module_name, scope);
                } else if (auto* CE = dyn_cast<ConstantExpr>(element)) {
                    // 处理常量表达式（如函数指针转换）
                    if (CE->getOpcode() == Instruction::BitCast) {
                        if (auto* func = dyn_cast<Function>(CE->getOperand(0))) {
                            std::string reason = "global_function_table_cast:" + GV->getName().str();
                            int confidence = 80;
                            
                            SymbolScope scope = global_symbols->global_functions.find(func->getName().str()) != 
                                               global_symbols->global_functions.end() ? 
                                               SymbolScope::GLOBAL : SymbolScope::STATIC;
                            
                            std::string module_name = global_symbols->function_to_module[func]->getName().str();
                            candidates.emplace_back(func, confidence, reason, module_name, scope);
                        }
                    }
                }
            }
        }
        // 分析结构体初始化器
        else if (auto* CS = dyn_cast<ConstantStruct>(init)) {
            for (unsigned i = 0; i < CS->getNumOperands(); ++i) {
                Value* field = CS->getOperand(i);
                
                if (auto* func = dyn_cast<Function>(field)) {
                    std::string reason = "global_struct_field:" + GV->getName().str();
                    int confidence = 83;
                    
                    SymbolScope scope = global_symbols->global_functions.find(func->getName().str()) != 
                                       global_symbols->global_functions.end() ? 
                                       SymbolScope::GLOBAL : SymbolScope::STATIC;
                    
                    std::string module_name = global_symbols->function_to_module[func]->getName().str();
                    candidates.emplace_back(func, confidence, reason, module_name, scope);
                }
            }
        }
    }
    
    return candidates;
}

InterruptHandlerAnalysis DeepFunctionPointerAnalyzer::analyzeCandidateFunction(Function* F) {
    // 避免重复分析
    if (analyzed_functions.find(F) != analyzed_functions.end()) {
        return InterruptHandlerAnalysis(); // 返回空分析
    }
    
    analyzed_functions.insert(F);
    
    InterruptHandlerAnalysis analysis;
    
    // 基本信息
    analysis.function_name = F->getName().str();
    analysis.is_confirmed_irq_handler = false; // 这是候选函数，不是确认的中断处理函数
    analysis.basic_block_count = F->size();
    
    // 获取模块信息
    Module* owner_module = global_symbols->function_to_module[F];
    if (owner_module) {
        analysis.source_file = owner_module->getName().str();
    }
    
    // 简单的内存访问分析（不递归分析函数指针）
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* LI = dyn_cast<LoadInst>(&I)) {
                MemoryAccessInfo access;
                access.is_write = false;
                access.confidence = 60; // 候选函数的访问置信度较低
                
                Value* ptr = LI->getPointerOperand();
                DataFlowNode flow_info = dataflow_analyzer->getDataFlowInfo(ptr);
                
                if (flow_info.node_type == "global") {
                    access.type = MemoryAccessInfo::GLOBAL_VARIABLE;
                    access.symbol_name = flow_info.source_info;
                    access.chain_description = "candidate_function_global_access";
                } else if (flow_info.node_type == "static") {
                    access.type = MemoryAccessInfo::GLOBAL_VARIABLE; // 静态变量也归类为全局
                    access.symbol_name = flow_info.source_info;
                    access.chain_description = "candidate_function_static_access";
                } else {
                    access.type = MemoryAccessInfo::INDIRECT_ACCESS;
                    access.symbol_name = "unknown";
                    access.chain_description = "candidate_function_indirect_access";
                }
                
                analysis.total_memory_accesses.push_back(access);
            } else if (auto* SI = dyn_cast<StoreInst>(&I)) {
                MemoryAccessInfo access;
                access.is_write = true;
                access.confidence = 60;
                
                Value* ptr = SI->getPointerOperand();
                DataFlowNode flow_info = dataflow_analyzer->getDataFlowInfo(ptr);
                
                if (flow_info.node_type == "global") {
                    access.type = MemoryAccessInfo::GLOBAL_VARIABLE;
                    access.symbol_name = flow_info.source_info;
                    access.chain_description = "candidate_function_global_write";
                } else if (flow_info.node_type == "static") {
                    access.type = MemoryAccessInfo::GLOBAL_VARIABLE;
                    access.symbol_name = flow_info.source_info;
                    access.chain_description = "candidate_function_static_write";
                } else {
                    access.type = MemoryAccessInfo::INDIRECT_ACCESS;
                    access.symbol_name = "unknown";
                    access.chain_description = "candidate_function_indirect_write";
                }
                
                analysis.total_memory_accesses.push_back(access);
            }
        }
    }
    
    return analysis;
}
