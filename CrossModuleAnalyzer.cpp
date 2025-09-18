//===- DeepFunctionPointerAnalyzer.cpp - 深度函数指针分析器实现 -----------===//

#include "CrossModuleAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// DeepFunctionPointerAnalyzer 主要实现
//===----------------------------------------------------------------------===//

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeDeep(Value* fp_value) {
    std::vector<FunctionPointerCandidate> candidates;
    
    if (!fp_value) {
        return candidates;
    }
    
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
    
    // 5. 数据流分析增强
    if (dataflow_analyzer) {
        DataFlowNode flow_info = dataflow_analyzer->getDataFlowInfo(fp_value);
        if (flow_info.node_type == "global" || flow_info.node_type == "static") {
            // 根据数据流信息调整候选函数的置信度
            for (auto& candidate : candidates) {
                if (flow_info.source_module && 
                    global_symbols->function_to_module[candidate.function] == flow_info.source_module) {
                    candidate.confidence += 15; // 同模块的函数获得额外置信度
                    candidate.match_reason += "_same_module";
                }
            }
        }
    }
    
    // 6. 去重并排序
    return processAndSortCandidates(candidates);
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::findCandidatesByType(FunctionType* FT) {
    std::vector<FunctionPointerCandidate> candidates;
    
    if (!FT) {
        return candidates;
    }
    
    // 构建函数类型签名
    std::string signature = buildFunctionSignature(FT);
    
    // 查找匹配的函数
    auto it = global_symbols->signature_to_functions.find(signature);
    if (it != global_symbols->signature_to_functions.end()) {
        for (Function* F : it->second) {
            if (!F) continue;
            
            std::string reason = "signature_match";
            int confidence = 50;
            
            // 根据函数名调整置信度
            confidence += analyzeFunctionNamePattern(F->getName().str(), reason);
            
            // 获取函数作用域
            SymbolScope scope = determineFunctionScope(F);
            if (scope == SymbolScope::STATIC) {
                reason += "_static_function";
            }
            
            std::string module_name = getModuleName(F);
            candidates.emplace_back(F, confidence, reason, module_name, scope);
        }
    }
    
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeFunctionPointerStorage(Value* fp_value) {
    std::vector<FunctionPointerCandidate> candidates;
    
    if (!fp_value) {
        return candidates;
    }
    
    // 查找对这个值的所有存储操作
    for (auto* User : fp_value->users()) {
        if (auto* SI = dyn_cast<StoreInst>(User)) {
            Value* stored_value = SI->getValueOperand();
            
            if (auto* F = dyn_cast<Function>(stored_value)) {
                std::string reason = "stored_function_pointer";
                int confidence = 75;
                
                // 检查存储位置的数据流信息
                if (dataflow_analyzer) {
                    DataFlowNode store_location = dataflow_analyzer->getDataFlowInfo(SI->getPointerOperand());
                    if (store_location.node_type == "global") {
                        confidence += 10;
                        reason += "_global_storage";
                    } else if (store_location.node_type == "static") {
                        confidence += 5;
                        reason += "_static_storage";
                    }
                }
                
                SymbolScope scope = determineFunctionScope(F);
                std::string module_name = getModuleName(F);
                candidates.emplace_back(F, confidence, reason, module_name, scope);
            }
        }
    }
    
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeStructFunctionPointers(GetElementPtrInst* GEP) {
    std::vector<FunctionPointerCandidate> candidates;
    
    if (!GEP) {
        return candidates;
    }
    
    Type* source_type = GEP->getSourceElementType();
    auto* struct_type = dyn_cast<StructType>(source_type);
    if (!struct_type) {
        return candidates;
    }
    
    std::string struct_name = struct_type->getName().str();
    
    // 分析这个结构体类型在所有模块中的使用
    for (const auto& module_pair : global_symbols->module_by_name) {
        Module* module = module_pair.second;
        if (!module) continue;
        
        // 查找对这个结构体字段的赋值
        for (auto& F : *module) {
            for (auto& BB : F) {
                for (auto& I : BB) {
                    if (auto* SI = dyn_cast<StoreInst>(&I)) {
                        if (auto* other_GEP = dyn_cast<GetElementPtrInst>(SI->getPointerOperand())) {
                            if (isMatchingStructField(GEP, other_GEP, struct_type)) {
                                Value* stored_func = SI->getValueOperand();
                                if (auto* func = dyn_cast<Function>(stored_func)) {
                                    std::string reason = "struct_field_assignment:" + struct_name;
                                    int confidence = 80;
                                    
                                    SymbolScope scope = determineFunctionScope(func);
                                    std::string module_name = getModuleName(func);
                                    candidates.emplace_back(func, confidence, reason, module_name, scope);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeGlobalFunctionTable(GlobalVariable* GV) {
    std::vector<FunctionPointerCandidate> candidates;
    
    if (!GV || !GV->hasInitializer()) {
        return candidates;
    }
    
    Constant* init = GV->getInitializer();
    std::string table_name = GV->getName().str();
    
    // 分析数组初始化器
    if (auto* CA = dyn_cast<ConstantArray>(init)) {
        for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
            Value* element = CA->getOperand(i);
            analyzeFunctionTableElement(element, table_name, "global_function_table", candidates);
        }
    }
    // 分析结构体初始化器
    else if (auto* CS = dyn_cast<ConstantStruct>(init)) {
        for (unsigned i = 0; i < CS->getNumOperands(); ++i) {
            Value* field = CS->getOperand(i);
            analyzeFunctionTableElement(field, table_name, "global_struct_field", candidates);
        }
    }
    
    return candidates;
}

InterruptHandlerAnalysis DeepFunctionPointerAnalyzer::analyzeCandidateFunction(Function* F) {
    if (!F) {
        return InterruptHandlerAnalysis();
    }
    
    // 避免重复分析
    if (analyzed_functions.find(F) != analyzed_functions.end()) {
        return InterruptHandlerAnalysis();
    }
    
    analyzed_functions.insert(F);
    
    InterruptHandlerAnalysis analysis;
    
    // 基本信息
    analysis.function_name = F->getName().str();
    analysis.is_confirmed_irq_handler = false; // 候选函数
    analysis.basic_block_count = F->size();
    
    // 获取模块信息
    Module* owner_module = global_symbols->function_to_module[F];
    if (owner_module) {
        analysis.source_file = owner_module->getName().str();
    }
    
    // 简单的内存访问分析（避免递归分析函数指针）
    analyzeBasicMemoryAccess(F, analysis);
    
    return analysis;
}

//===----------------------------------------------------------------------===//
// 辅助函数实现
//===----------------------------------------------------------------------===//

std::string DeepFunctionPointerAnalyzer::buildFunctionSignature(FunctionType* FT) {
    if (!FT) {
        return "";
    }
    
    std::string signature;
    signature += std::to_string(FT->getReturnType()->getTypeID()) + "_";
    
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
        signature += std::to_string(FT->getParamType(i)->getTypeID()) + "_";
    }
    
    return signature;
}

int DeepFunctionPointerAnalyzer::analyzeFunctionNamePattern(const std::string& name, std::string& reason) {
    int confidence_boost = 0;
    
    // 检查回调函数命名模式
    if (name.find("callback") != std::string::npos || 
        name.find("handler") != std::string::npos || 
        name.find("interrupt") != std::string::npos || 
        name.find("irq") != std::string::npos) {
        confidence_boost += 20;
        reason += "_callback_pattern";
    }
    
    // 检查函数后缀
    if (name.size() >= 3 && 
        (name.substr(name.size() - 3) == "_fn" || 
         name.substr(name.size() - 5) == "_func")) {
        confidence_boost += 10;
        reason += "_function_suffix";
    }
    
    return confidence_boost;
}

SymbolScope DeepFunctionPointerAnalyzer::determineFunctionScope(Function* F) {
    if (!F) {
        return SymbolScope::STATIC;
    }
    
    auto global_it = global_symbols->global_functions.find(F->getName().str());
    return (global_it != global_symbols->global_functions.end()) ? SymbolScope::GLOBAL : SymbolScope::STATIC;
}

std::string DeepFunctionPointerAnalyzer::getModuleName(Function* F) {
    if (!F) {
        return "unknown";
    }
    
    auto module_it = global_symbols->function_to_module.find(F);
    return (module_it != global_symbols->function_to_module.end()) ? 
           module_it->second->getName().str() : "unknown";
}

bool DeepFunctionPointerAnalyzer::isMatchingStructField(GetElementPtrInst* gep1, GetElementPtrInst* gep2, StructType* expected_type) {
    if (!gep1 || !gep2 || !expected_type) {
        return false;
    }
    
    // 检查是否是同一个结构体字段
    if (gep2->getSourceElementType() == expected_type &&
        gep1->getNumOperands() == gep2->getNumOperands()) {
        
        // 比较所有操作数
        for (unsigned i = 1; i < gep1->getNumOperands(); ++i) {
            if (gep1->getOperand(i) != gep2->getOperand(i)) {
                return false;
            }
        }
        return true;
    }
    
    return false;
}

void DeepFunctionPointerAnalyzer::analyzeFunctionTableElement(Value* element, const std::string& table_name, 
                                                             const std::string& element_type, 
                                                             std::vector<FunctionPointerCandidate>& candidates) {
    if (!element) {
        return;
    }
    
    Function* func = nullptr;
    int confidence = 85;
    std::string reason = element_type + ":" + table_name;
    
    if (auto* direct_func = dyn_cast<Function>(element)) {
        func = direct_func;
    } else if (auto* CE = dyn_cast<ConstantExpr>(element)) {
        // 处理常量表达式（如函数指针转换）
        if (CE->getOpcode() == Instruction::BitCast && CE->getNumOperands() > 0) {
            if (auto* cast_func = dyn_cast<Function>(CE->getOperand(0))) {
                func = cast_func;
                confidence = 80;
                reason += "_cast";
            }
        }
    }
    
    if (func) {
        SymbolScope scope = determineFunctionScope(func);
        std::string module_name = getModuleName(func);
        candidates.emplace_back(func, confidence, reason, module_name, scope);
    }
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::processAndSortCandidates(
    std::vector<FunctionPointerCandidate> candidates) {
    
    if (candidates.empty()) {
        return candidates;
    }
    
    // 排序（按置信度降序）
    std::sort(candidates.begin(), candidates.end(), 
              [](const FunctionPointerCandidate& a, const FunctionPointerCandidate& b) {
                  return a.confidence > b.confidence;
              });
    
    // 去重（保留置信度最高的）
    std::vector<FunctionPointerCandidate> unique_candidates;
    std::set<Function*> seen_functions;
    
    for (const auto& candidate : candidates) {
        if (seen_functions.find(candidate.function) == seen_functions.end()) {
            seen_functions.insert(candidate.function);
            unique_candidates.push_back(candidate);
        }
    }
    
    // 标记需要进一步分析的候选函数
    for (auto& candidate : unique_candidates) {
        if (candidate.confidence >= 60 && 
            analyzed_functions.find(candidate.function) == analyzed_functions.end()) {
            candidate.requires_further_analysis = true;
        }
    }
    
    return unique_candidates;
}

void DeepFunctionPointerAnalyzer::analyzeBasicMemoryAccess(Function* F, InterruptHandlerAnalysis& analysis) {
    if (!F) {
        return;
    }
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            MemoryAccessInfo access;
            bool is_memory_access = false;
            
            if (auto* LI = dyn_cast<LoadInst>(&I)) {
                access.is_write = false;
                access.confidence = 60;
                is_memory_access = true;
                
                if (dataflow_analyzer) {
                    analyzeMemoryAccessWithDataFlow(LI->getPointerOperand(), access);
                }
                
            } else if (auto* SI = dyn_cast<StoreInst>(&I)) {
                access.is_write = true;
                access.confidence = 60;
                is_memory_access = true;
                
                if (dataflow_analyzer) {
                    analyzeMemoryAccessWithDataFlow(SI->getPointerOperand(), access);
                }
            }
            
            if (is_memory_access) {
                analysis.total_memory_accesses.push_back(access);
            }
        }
    }
}

void DeepFunctionPointerAnalyzer::analyzeMemoryAccessWithDataFlow(Value* ptr, MemoryAccessInfo& access) {
    if (!ptr || !dataflow_analyzer) {
        access.type = MemoryAccessInfo::INDIRECT_ACCESS;
        access.symbol_name = "unknown";
        access.chain_description = "candidate_function_unknown_access";
        return;
    }
    
    DataFlowNode flow_info = dataflow_analyzer->getDataFlowInfo(ptr);
    
    if (flow_info.node_type == "global") {
        access.type = MemoryAccessInfo::GLOBAL_VARIABLE;
        access.symbol_name = flow_info.source_info;
        access.chain_description = "candidate_function_global_access";
    } else if (flow_info.node_type == "static") {
        access.type = MemoryAccessInfo::GLOBAL_VARIABLE;
        access.symbol_name = flow_info.source_info;
        access.chain_description = "candidate_function_static_access";
    } else {
        access.type = MemoryAccessInfo::INDIRECT_ACCESS;
        access.symbol_name = "unknown";
        access.chain_description = "candidate_function_indirect_access";
    }
}
