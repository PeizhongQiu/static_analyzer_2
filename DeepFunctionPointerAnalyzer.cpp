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
    
    // 添加详细调试信息
    errs() << "DEBUG: DeepFunctionPointerAnalyzer::analyzeDeep called\n";
    errs() << "DEBUG: Analyzing potential function pointer: " << *fp_value << "\n";
    errs() << "DEBUG: Value type: " << *fp_value->getType() << "\n";
    
    // 检查是否真的是函数指针类型
    if (!fp_value->getType()->isPointerTy()) {
        errs() << "DEBUG: Not a pointer type, returning empty candidates\n";
        return candidates;
    }
    
    Type* pointee_type = fp_value->getType()->getPointerElementType();
    if (!pointee_type->isFunctionTy()) {
        errs() << "DEBUG: Not pointing to function type, pointee: " << *pointee_type << "\n";
        errs() << "DEBUG: This might be a complex function pointer access pattern\n";
    }
    
    // 检查是否是常量函数指针（直接调用的变形）
    if (auto* func = dyn_cast<Function>(fp_value)) {
        errs() << "DEBUG: This is a direct function reference, not a true function pointer\n";
        errs() << "DEBUG: Function: " << func->getName() << "\n";
        // 这不应该被视为间接调用
        return candidates;
    }
    
    // 检查是否是常量表达式
    if (auto* CE = dyn_cast<ConstantExpr>(fp_value)) {
        errs() << "DEBUG: Constant expression detected: " << *CE << "\n";
        if (CE->getOpcode() == Instruction::BitCast) {
            if (auto* func = dyn_cast<Function>(CE->getOperand(0))) {
                errs() << "DEBUG: Constant expression is just a bitcast of direct function: " << func->getName() << "\n";
                // 这也不是真正的间接调用
                return candidates;
            }
        }
    }
    
    // 1. 基于类型的候选函数查找
    if (auto* ptr_type = dyn_cast<PointerType>(fp_value->getType())) {
        if (auto* func_type = dyn_cast<FunctionType>(ptr_type->getElementType())) {
            errs() << "DEBUG: Found function pointer type, searching for candidates by signature\n";
            auto type_candidates = findCandidatesByType(func_type);
            errs() << "DEBUG: Found " << type_candidates.size() << " candidates by type matching\n";
            candidates.insert(candidates.end(), type_candidates.begin(), type_candidates.end());
        } else {
            errs() << "DEBUG: Pointer to non-function type, analyzing as complex function pointer\n";
        }
    }
    
    // 2. 基于存储位置的分析
    errs() << "DEBUG: Analyzing function pointer storage patterns\n";
    auto storage_candidates = analyzeFunctionPointerStorage(fp_value);
    errs() << "DEBUG: Found " << storage_candidates.size() << " candidates by storage analysis\n";
    candidates.insert(candidates.end(), storage_candidates.begin(), storage_candidates.end());
    
    // 3. 如果是GEP指令，分析结构体字段
    if (auto* GEP = dyn_cast<GetElementPtrInst>(fp_value)) {
        errs() << "DEBUG: Analyzing GEP instruction for struct function pointers\n";
        auto struct_candidates = analyzeStructFunctionPointers(GEP);
        errs() << "DEBUG: Found " << struct_candidates.size() << " candidates by struct analysis\n";
        candidates.insert(candidates.end(), struct_candidates.begin(), struct_candidates.end());
    }
    
    // 4. 如果是全局变量，分析函数指针表
    if (auto* GV = dyn_cast<GlobalVariable>(fp_value)) {
        errs() << "DEBUG: Analyzing global variable function pointer table\n";
        auto table_candidates = analyzeGlobalFunctionTable(GV);
        errs() << "DEBUG: Found " << table_candidates.size() << " candidates by global table analysis\n";
        candidates.insert(candidates.end(), table_candidates.begin(), table_candidates.end());
    }
    
    // 5. 数据流分析
    errs() << "DEBUG: Performing dataflow analysis\n";
    DataFlowNode flow_info = dataflow_analyzer->getDataFlowInfo(fp_value);
    errs() << "DEBUG: Dataflow node type: " << flow_info.node_type << "\n";
    errs() << "DEBUG: Dataflow confidence: " << flow_info.confidence << "\n";
    
    if (flow_info.node_type == "global" || flow_info.node_type == "static") {
        // 根据数据流信息调整候选函数的置信度
        for (auto& candidate : candidates) {
            if (flow_info.source_module && 
                global_symbols->function_to_module[candidate.function] == flow_info.source_module) {
                candidate.confidence += 15; // 同模块的函数获得额外置信度
                errs() << "DEBUG: Boosted confidence for same-module function: " << candidate.function->getName() << "\n";
            }
        }
    }
    
    // 如果没有找到任何候选函数，记录调试信息
    if (candidates.empty()) {
        errs() << "DEBUG: No function pointer candidates found\n";
        errs() << "DEBUG: This suggests either:\n";
        errs() << "DEBUG: 1. Not a true indirect call\n";
        errs() << "DEBUG: 2. Complex function pointer pattern not handled\n";
        errs() << "DEBUG: 3. Missing function definitions in symbol table\n";
        
        // 检查全局符号表统计
        errs() << "DEBUG: Global symbol table stats:\n";
        errs() << "DEBUG: Global functions: " << global_symbols->global_functions.size() << "\n";
        errs() << "DEBUG: Total function signatures: " << global_symbols->signature_to_functions.size() << "\n";
        
        return candidates;
    }
    
    // 去重并排序
    errs() << "DEBUG: Processing " << candidates.size() << " raw candidates\n";
    
    std::sort(candidates.begin(), candidates.end(), 
              [](const FunctionPointerCandidate& a, const FunctionPointerCandidate& b) {
                  return a.confidence > b.confidence;
              });
    
    // 移除重复项
    auto original_size = candidates.size();
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                [](const FunctionPointerCandidate& a, const FunctionPointerCandidate& b) {
                                    return a.function == b.function;
                                }), candidates.end());
    
    errs() << "DEBUG: After deduplication: " << candidates.size() << " candidates (removed " << (original_size - candidates.size()) << " duplicates)\n";
    
    // 标记需要进一步分析的候选函数
    int marked_for_analysis = 0;
    for (auto& candidate : candidates) {
        if (candidate.confidence >= 60 && 
            analyzed_functions.find(candidate.function) == analyzed_functions.end()) {
            candidate.requires_further_analysis = true;
            marked_for_analysis++;
        }
    }
    
    errs() << "DEBUG: Marked " << marked_for_analysis << " candidates for further analysis\n";
    
    // 输出最终的候选函数列表
    errs() << "DEBUG: Final candidate list:\n";
    for (const auto& candidate : candidates) {
        errs() << "DEBUG:   " << candidate.function->getName() 
               << " (confidence: " << candidate.confidence 
               << ", reason: " << candidate.match_reason << ")\n";
    }
    
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::findCandidatesByType(FunctionType* FT) {
    std::vector<FunctionPointerCandidate> candidates;
    
    errs() << "DEBUG: findCandidatesByType called\n";
    errs() << "DEBUG: Target function type: " << *FT << "\n";
    errs() << "DEBUG: Return type: " << *FT->getReturnType() << "\n";
    errs() << "DEBUG: Parameter count: " << FT->getNumParams() << "\n";
    
    // 构建函数类型签名
    std::string signature;
    signature += std::to_string(FT->getReturnType()->getTypeID()) + "_";
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
        signature += std::to_string(FT->getParamType(i)->getTypeID()) + "_";
        errs() << "DEBUG: Param " << i << ": " << *FT->getParamType(i) << "\n";
    }
    
    errs() << "DEBUG: Built signature: " << signature << "\n";
    
    // 查找匹配的函数
    auto it = global_symbols->signature_to_functions.find(signature);
    if (it == global_symbols->signature_to_functions.end()) {
        errs() << "DEBUG: No functions found with matching signature\n";
        return candidates;
    }
    
    errs() << "DEBUG: Found " << it->second.size() << " functions with matching signature\n";
    
    for (Function* F : it->second) {
        std::string reason = "signature_match";
        int confidence = 50;
        
        errs() << "DEBUG: Evaluating function: " << F->getName() << "\n";
        
        // 根据函数名调整置信度
        StringRef name = F->getName();
        if (name.contains("callback") || name.contains("handler") || 
            name.contains("interrupt") || name.contains("irq")) {
            confidence += 20;
            reason += "_callback_pattern";
            errs() << "DEBUG: Boosted confidence for callback pattern in: " << name << "\n";
        }
        
        if (name.endswith("_fn") || name.endswith("_func")) {
            confidence += 10;
            reason += "_function_suffix";
        }
        
        // 检查函数是否实际存在并且不是声明
        if (F->isDeclaration()) {
            confidence -= 10;
            reason += "_declaration_only";
            errs() << "DEBUG: Reduced confidence for declaration-only function: " << name << "\n";
        }
        
        // 过滤明显不相关的函数（基于语义）
        if (name.contains("idt_setup") || name.contains("dummy_handler") ||
            name.contains("syscall") || name.contains("trap")) {
            confidence -= 30;
            reason += "_semantic_mismatch";
            errs() << "DEBUG: Reduced confidence for semantically mismatched function: " << name << "\n";
        }
        
        // 只保留合理置信度的候选函数
        if (confidence < 30) {
            errs() << "DEBUG: Filtering out low-confidence function: " << name << " (confidence: " << confidence << ")\n";
            continue;
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
        
        errs() << "DEBUG: Added candidate: " << name << " (confidence: " << confidence << ", scope: " << 
                  (scope == SymbolScope::GLOBAL ? "global" : "static") << ")\n";
    }
    
    errs() << "DEBUG: findCandidatesByType returning " << candidates.size() << " candidates\n";
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeFunctionPointerStorage(Value* fp_value) {
    std::vector<FunctionPointerCandidate> candidates;
    
    errs() << "DEBUG: analyzeFunctionPointerStorage called\n";
    
    // 查找对这个值的所有存储操作
    for (auto* User : fp_value->users()) {
        if (auto* SI = dyn_cast<StoreInst>(User)) {
            Value* stored_value = SI->getValueOperand();
            
            errs() << "DEBUG: Found store instruction, stored value: " << *stored_value << "\n";
            
            if (auto* F = dyn_cast<Function>(stored_value)) {
                std::string reason = "stored_function_pointer";
                int confidence = 75;
                
                errs() << "DEBUG: Stored value is function: " << F->getName() << "\n";
                
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
    
    errs() << "DEBUG: analyzeFunctionPointerStorage returning " << candidates.size() << " candidates\n";
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeStructFunctionPointers(GetElementPtrInst* GEP) {
    std::vector<FunctionPointerCandidate> candidates;
    
    errs() << "DEBUG: analyzeStructFunctionPointers called\n";
    errs() << "DEBUG: GEP instruction: " << *GEP << "\n";
    
    Type* source_type = GEP->getSourceElementType();
    if (auto* struct_type = dyn_cast<StructType>(source_type)) {
        std::string struct_name = struct_type->getName().str();
        errs() << "DEBUG: Analyzing struct type: " << struct_name << "\n";
        
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
                                            
                                            errs() << "DEBUG: Found struct field assignment: " << func->getName() << " to " << struct_name << "\n";
                                            
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
            }
        }
    }
    
    errs() << "DEBUG: analyzeStructFunctionPointers returning " << candidates.size() << " candidates\n";
    return candidates;
}

std::vector<FunctionPointerCandidate> DeepFunctionPointerAnalyzer::analyzeGlobalFunctionTable(GlobalVariable* GV) {
    std::vector<FunctionPointerCandidate> candidates;
    
    errs() << "DEBUG: analyzeGlobalFunctionTable called for: " << GV->getName() << "\n";
    
    if (GV->hasInitializer()) {
        Constant* init = GV->getInitializer();
        
        // 分析数组初始化器
        if (auto* CA = dyn_cast<ConstantArray>(init)) {
            errs() << "DEBUG: Analyzing constant array with " << CA->getNumOperands() << " elements\n";
            
            for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
                Value* element = CA->getOperand(i);
                
                if (auto* func = dyn_cast<Function>(element)) {
                    std::string reason = "global_function_table:" + GV->getName().str();
                    int confidence = 85;
                    
                    errs() << "DEBUG: Found function in array: " << func->getName() << "\n";
                    
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
                            
                            errs() << "DEBUG: Found function in array (via bitcast): " << func->getName() << "\n";
                            
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
            errs() << "DEBUG: Analyzing constant struct with " << CS->getNumOperands() << " fields\n";
            
            for (unsigned i = 0; i < CS->getNumOperands(); ++i) {
                Value* field = CS->getOperand(i);
                
                if (auto* func = dyn_cast<Function>(field)) {
                    std::string reason = "global_struct_field:" + GV->getName().str();
                    int confidence = 83;
                    
                    errs() << "DEBUG: Found function in struct field: " << func->getName() << "\n";
                    
                    SymbolScope scope = global_symbols->global_functions.find(func->getName().str()) != 
                                       global_symbols->global_functions.end() ? 
                                       SymbolScope::GLOBAL : SymbolScope::STATIC;
                    
                    std::string module_name = global_symbols->function_to_module[func]->getName().str();
                    candidates.emplace_back(func, confidence, reason, module_name, scope);
                }
            }
        }
    }
    
    errs() << "DEBUG: analyzeGlobalFunctionTable returning " << candidates.size() << " candidates\n";
    return candidates;
}

InterruptHandlerAnalysis DeepFunctionPointerAnalyzer::analyzeCandidateFunction(Function* F) {
    // 避免重复分析
    if (analyzed_functions.find(F) != analyzed_functions.end()) {
        errs() << "DEBUG: Function " << F->getName() << " already analyzed, returning empty analysis\n";
        return InterruptHandlerAnalysis(); // 返回空分析
    }
    
    analyzed_functions.insert(F);
    errs() << "DEBUG: Analyzing candidate function: " << F->getName() << "\n";
    
    InterruptHandlerAnalysis analysis;
    
    // 基本信息
    analysis.function_name = F->getName().str();
    analysis.is_confirmed_irq_handler = false; // 这是候选函数，不是确认的中断处理函数
    analysis.basic_block_count = F->size();
    
    // 获取模块信息
    Module* owner_module = global_symbols->function_to_module[F];
    if (owner_module) {
        analysis.source_file = owner_module->getName().str();
        errs() << "DEBUG: Function belongs to module: " << analysis.source_file << "\n";
    }
    
    // 简单的内存访问分析（不递归分析函数指针）
    int load_count = 0;
    int store_count = 0;
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* LI = dyn_cast<LoadInst>(&I)) {
                load_count++;
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
                store_count++;
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
    
    errs() << "DEBUG: Candidate function analysis complete:\n";
    errs() << "DEBUG:   Load instructions: " << load_count << "\n";
    errs() << "DEBUG:   Store instructions: " << store_count << "\n";
    errs() << "DEBUG:   Memory accesses identified: " << analysis.total_memory_accesses.size() << "\n";
    
    return analysis;
}
