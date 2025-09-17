//===- FunctionPointerAnalyzer.cpp - Function Pointer Analyzer Implementation ===//

#include "FunctionPointerAnalyzer.h"
#include "MemoryAccessAnalyzer.h"
#include "InlineAsmAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

FunctionPointerAnalyzer::FunctionPointerAnalyzer(Module *M, const DataLayout *DL) 
    : M(M), DL(DL) {
    buildFunctionSignatureMap();
}

void FunctionPointerAnalyzer::buildFunctionSignatureMap() {
    for (auto &F : *M) {
        if (F.isDeclaration()) continue;
        
        std::string signature = getFunctionSignature(&F);
        signature_to_functions[signature].push_back(&F);
    }
}

std::string FunctionPointerAnalyzer::getFunctionSignature(Function *F) {
    std::string sig;
    
    // 返回类型
    sig += std::to_string(F->getReturnType()->getTypeID());
    sig += "_";
    
    // 参数类型
    for (auto &Arg : F->args()) {
        sig += std::to_string(Arg.getType()->getTypeID()) + "_";
    }
    
    return sig;
}

std::string FunctionPointerAnalyzer::getValueName(Value *V) {
    if (V->hasName()) {
        return V->getName().str();
    } else if (auto *F = dyn_cast<Function>(V)) {
        return F->getName().str();
    } else {
        return "unnamed_" + std::to_string(reinterpret_cast<uintptr_t>(V));
    }
}

FunctionPointerAnalysis FunctionPointerAnalyzer::analyzeFunctionPointer(Value *fp_value) {
    // 检查缓存
    if (fp_analysis_cache.find(fp_value) != fp_analysis_cache.end()) {
        return fp_analysis_cache[fp_value];
    }
    
    FunctionPointerAnalysis analysis;
    analysis.function_pointer = fp_value;
    analysis.pointer_name = getValueName(fp_value);
    
    // 1. 直接函数引用
    if (auto *direct_func = dyn_cast<Function>(fp_value)) {
        analysis.possible_targets.emplace_back(direct_func, 100, "direct_reference");
        analysis.is_resolved = true;
    }
    // 2. 常量表达式中的函数
    else if (auto *CE = dyn_cast<ConstantExpr>(fp_value)) {
        analyzeConstantExpr(CE, analysis);
    }
    // 3. 全局变量中的函数指针
    else if (auto *GV = dyn_cast<GlobalVariable>(fp_value)) {
        analyzeGlobalVariableFP(GV, analysis);
    }
    // 4. 从内存加载的函数指针
    else if (auto *LI = dyn_cast<LoadInst>(fp_value)) {
        analyzeLoadedFunctionPointer(LI, analysis);
    }
    // 5. 结构体字段中的函数指针
    else if (auto *GEP = dyn_cast<GetElementPtrInst>(fp_value)) {
        analyzeStructFieldFunctionPointer(GEP, analysis);
    }
    // 6. PHI节点（控制流汇聚）
    else if (auto *PHI = dyn_cast<PHINode>(fp_value)) {
        analyzePHINodeFunctionPointer(PHI, analysis);
    }
    // 7. 函数参数传入的函数指针
    else if (auto *Arg = dyn_cast<Argument>(fp_value)) {
        analyzeArgumentFunctionPointer(Arg, analysis);
    }
    // 8. 基于类型的启发式分析
    else {
        performHeuristicAnalysis(fp_value, analysis);
    }
    
    // 缓存结果
    fp_analysis_cache[fp_value] = analysis;
    return analysis;
}

void FunctionPointerAnalyzer::analyzeConstantExpr(ConstantExpr *CE, FunctionPointerAnalysis &analysis) {
    if (CE->getOpcode() == Instruction::BitCast || 
        CE->getOpcode() == Instruction::IntToPtr) {
        Value *operand = CE->getOperand(0);
        if (auto *func = dyn_cast<Function>(operand)) {
            analysis.possible_targets.emplace_back(func, 95, "constant_expr_cast");
        }
    }
}

void FunctionPointerAnalyzer::analyzeGlobalVariableFP(GlobalVariable *GV, FunctionPointerAnalysis &analysis) {
    if (GV->hasInitializer()) {
        Constant *initializer = GV->getInitializer();
        if (auto *func = dyn_cast<Function>(initializer)) {
            analysis.possible_targets.emplace_back(func, 90, "global_initializer");
        } else if (auto *CE = dyn_cast<ConstantExpr>(initializer)) {
            analyzeConstantExpr(CE, analysis);
        }
    }
    
    // 查找对这个全局变量的赋值
    for (auto *User : GV->users()) {
        if (auto *SI = dyn_cast<StoreInst>(User)) {
            Value *stored_value = SI->getValueOperand();
            if (auto *func = dyn_cast<Function>(stored_value)) {
                analysis.possible_targets.emplace_back(func, 80, "global_assignment");
            }
        }
    }
}

void FunctionPointerAnalyzer::analyzeLoadedFunctionPointer(LoadInst *LI, FunctionPointerAnalysis &analysis) {
    Value *ptr = LI->getPointerOperand();
    
    // 追踪这个指针可能指向的内存位置
    if (auto *GV = dyn_cast<GlobalVariable>(ptr)) {
        analyzeGlobalVariableFP(GV, analysis);
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(ptr)) {
        analyzeStructFieldFunctionPointer(GEP, analysis);
    } else {
        // 查找所有可能存储到这个位置的函数指针
        analyzeStoresTo(ptr, analysis);
    }
}

void FunctionPointerAnalyzer::analyzeStructFieldFunctionPointer(GetElementPtrInst *GEP, FunctionPointerAnalysis &analysis) {
    Type *struct_type = GEP->getSourceElementType();
    
    if (auto *ST = dyn_cast<StructType>(struct_type)) {
        std::string struct_name = ST->getName().str();
        
        // 获取字段索引
        if (GEP->getNumOperands() >= 3) {
            if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                int field_index = CI->getZExtValue();
                
                // 查找所有对此结构体字段的赋值
                findFunctionPointersInStructField(struct_name, field_index, analysis);
            }
        }
    }
}

void FunctionPointerAnalyzer::analyzePHINodeFunctionPointer(PHINode *PHI, FunctionPointerAnalysis &analysis) {
    // PHI节点合并多个控制流路径的值
    for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
        Value *incoming = PHI->getIncomingValue(i);
        
        FunctionPointerAnalysis sub_analysis = analyzeFunctionPointer(incoming);
        
        // 合并可能的目标，但降低置信度
        for (auto &target : sub_analysis.possible_targets) {
            target.confidence = (target.confidence * 80) / 100; // 降低20%置信度
            target.analysis_reason += "_via_phi";
            analysis.possible_targets.push_back(target);
        }
    }
    
    // 去重
    removeDuplicateTargets(analysis);
}

void FunctionPointerAnalyzer::analyzeArgumentFunctionPointer(Argument *Arg, FunctionPointerAnalysis &analysis) {
    Function *F = Arg->getParent();
    
    // 查找所有调用此函数的地方，看传入了什么函数指针
    for (auto *User : F->users()) {
        if (auto *CI = dyn_cast<CallInst>(User)) {
            if (CI->getCalledFunction() == F) {
                Value *arg_value = CI->getArgOperand(Arg->getArgNo());
                
                if (auto *func = dyn_cast<Function>(arg_value)) {
                    analysis.possible_targets.emplace_back(func, 70, "argument_from_caller");
                } else {
                    // 递归分析传入的参数
                    FunctionPointerAnalysis sub_analysis = analyzeFunctionPointer(arg_value);
                    for (auto &target : sub_analysis.possible_targets) {
                        target.confidence = (target.confidence * 70) / 100;
                        analysis.possible_targets.push_back(target);
                    }
                }
            }
        }
    }
}

void FunctionPointerAnalyzer::performHeuristicAnalysis(Value *fp_value, FunctionPointerAnalysis &analysis) {
    // 基于类型匹配的启发式分析
    Type *fp_type = fp_value->getType();
    
    if (auto *ptr_type = dyn_cast<PointerType>(fp_type)) {
        if (auto *func_type = dyn_cast<FunctionType>(ptr_type->getElementType())) {
            std::string signature = getFunctionTypeSignature(func_type);
            
            // 查找签名匹配的函数
            if (signature_to_functions.find(signature) != signature_to_functions.end()) {
                for (Function *candidate : signature_to_functions[signature]) {
                    // 只添加看起来像中断处理或回调函数的
                    if (isLikelyCallbackFunction(candidate)) {
                        analysis.possible_targets.emplace_back(candidate, 40, "signature_match_heuristic");
                    }
                }
            }
        }
    }
}

void FunctionPointerAnalyzer::analyzeStoresTo(Value *ptr, FunctionPointerAnalysis &analysis) {
    // 查找所有存储到这个指针位置的指令
    for (auto *User : ptr->users()) {
        if (auto *SI = dyn_cast<StoreInst>(User)) {
            if (SI->getPointerOperand() == ptr) {
                Value *stored_value = SI->getValueOperand();
                
                if (auto *func = dyn_cast<Function>(stored_value)) {
                    analysis.possible_targets.emplace_back(func, 75, "store_instruction");
                } else {
                    // 递归分析存储的值
                    FunctionPointerAnalysis sub_analysis = analyzeFunctionPointer(stored_value);
                    for (auto &target : sub_analysis.possible_targets) {
                        target.confidence = (target.confidence * 75) / 100;
                        analysis.possible_targets.push_back(target);
                    }
                }
            }
        }
    }
}

void FunctionPointerAnalyzer::findFunctionPointersInStructField(const std::string &struct_name, int field_index, 
                                                               FunctionPointerAnalysis &analysis) {
    // 查找所有此结构体类型的实例，以及对相应字段的赋值
    for (auto &F : *M) {
        if (F.isDeclaration()) continue;
        
        for (auto &BB : F) {
            for (auto &I : BB) {
                if (auto *SI = dyn_cast<StoreInst>(&I)) {
                    Value *ptr = SI->getPointerOperand();
                    
                    if (auto *GEP = dyn_cast<GetElementPtrInst>(ptr)) {
                        if (matchesStructField(GEP, struct_name, field_index)) {
                            Value *stored_func = SI->getValueOperand();
                            if (auto *func = dyn_cast<Function>(stored_func)) {
                                analysis.possible_targets.emplace_back(func, 85, "struct_field_assignment");
                            }
                        }
                    }
                }
            }
        }
    }
}

bool FunctionPointerAnalyzer::matchesStructField(GetElementPtrInst *GEP, const std::string &struct_name, int field_index) {
    Type *source_type = GEP->getSourceElementType();
    if (auto *ST = dyn_cast<StructType>(source_type)) {
        if (ST->getName() == struct_name && GEP->getNumOperands() >= 3) {
            if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                return CI->getZExtValue() == field_index;
            }
        }
    }
    return false;
}

std::string FunctionPointerAnalyzer::getFunctionTypeSignature(FunctionType *FT) {
    std::string sig;
    sig += std::to_string(FT->getReturnType()->getTypeID()) + "_";
    
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
        sig += std::to_string(FT->getParamType(i)->getTypeID()) + "_";
    }
    
    return sig;
}

bool FunctionPointerAnalyzer::isLikelyCallbackFunction(Function *F) {
    StringRef name = F->getName();
    
    // 常见的回调函数名称模式
    return name.contains("callback") || name.contains("handler") || 
           name.contains("interrupt") || name.contains("irq") ||
           name.contains("tasklet") || name.contains("work") ||
           name.endswith("_fn") || name.endswith("_func");
}

void FunctionPointerAnalyzer::removeDuplicateTargets(FunctionPointerAnalysis &analysis) {
    std::sort(analysis.possible_targets.begin(), analysis.possible_targets.end(),
             [](const FunctionPointerTarget &a, const FunctionPointerTarget &b) {
                 return a.target_function < b.target_function;
             });
    
    analysis.possible_targets.erase(
        std::unique(analysis.possible_targets.begin(), analysis.possible_targets.end(),
                   [](const FunctionPointerTarget &a, const FunctionPointerTarget &b) {
                       return a.target_function == b.target_function;
                   }),
        analysis.possible_targets.end());
}

IndirectCallAnalysis FunctionPointerAnalyzer::analyzeIndirectCall(CallInst *CI) {
    IndirectCallAnalysis analysis;
    analysis.call_inst = CI;
    
    Value *called_value = CI->getCalledOperand();
    analysis.fp_analysis = analyzeFunctionPointer(called_value);
    
    // 聚合所有可能目标函数的内存访问
    for (const auto &target : analysis.fp_analysis.possible_targets) {
        if (!target.target_function) continue;
        
        auto mem_accesses = getFunctionMemoryAccesses(target.target_function);
        
        // 根据置信度加权
        for (auto access : mem_accesses) {
            access.confidence = (access.confidence * target.confidence) / 100;
            analysis.aggregated_accesses.push_back(access);
        }
        
        // 聚合寄存器访问
        auto reg_accesses = getFunctionRegisterAccesses(target.target_function);
        analysis.aggregated_register_accesses.insert(
            analysis.aggregated_register_accesses.end(),
            reg_accesses.begin(), reg_accesses.end());
    }
    
    return analysis;
}

std::vector<MemoryAccessInfo> FunctionPointerAnalyzer::getFunctionMemoryAccesses(Function *F) {
    // 检查缓存
    if (function_memory_cache.find(F) != function_memory_cache.end()) {
        return function_memory_cache[F];
    }
    
    // 使用内存访问分析器分析这个函数
    MemoryAccessAnalyzer analyzer(DL);
    std::vector<MemoryAccessInfo> accesses = analyzer.analyzeFunction(*F);
    
    // 缓存结果
    function_memory_cache[F] = accesses;
    return accesses;
}

std::vector<RegisterAccessInfo> FunctionPointerAnalyzer::getFunctionRegisterAccesses(Function *F) {
    std::vector<RegisterAccessInfo> reg_accesses;
    
    // 分析内联汇编
    InlineAsmAnalyzer asm_analyzer;
    for (auto &BB : *F) {
        for (auto &I : BB) {
            if (auto *CI = dyn_cast<CallInst>(&I)) {
                if (auto *IA = dyn_cast<InlineAsm>(CI->getCalledOperand())) {
                    auto regs = asm_analyzer.analyzeInlineAsm(IA);
                    reg_accesses.insert(reg_accesses.end(), regs.begin(), regs.end());
                }
            }
        }
    }
    
    return reg_accesses;
}
