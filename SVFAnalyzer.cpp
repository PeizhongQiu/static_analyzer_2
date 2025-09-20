//===- SVFAnalyzer.cpp - SVF Integration Implementation ------------------===//

#include "SVFAnalyzer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include <chrono>
#include <algorithm>

using namespace llvm;

//===----------------------------------------------------------------------===//
// SVF可用性检查
//===----------------------------------------------------------------------===//

bool SVFAnalyzer::isSVFAvailable() {
#ifdef SVF_AVAILABLE
    return true;
#else
    return false;
#endif
}

std::string SVFAnalyzer::getSVFVersion() {
#ifdef SVF_AVAILABLE
    return "SVF-2.6+"; // 实际版本需要从SVF获取
#else
    return "SVF Not Available";
#endif
}

//===----------------------------------------------------------------------===//
// SVF初始化
//===----------------------------------------------------------------------===//

bool SVFAnalyzer::initialize(const std::vector<std::unique_ptr<Module>>& modules) {
#ifndef SVF_AVAILABLE
    errs() << "Warning: SVF not available, falling back to basic analysis\n";
    return false;
#else
    if (modules.empty()) {
        errs() << "Error: No modules provided for SVF analysis\n";
        return false;
    }
    
    outs() << "Initializing SVF analysis framework...\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // 初始化SVF选项
        SVF::Options::EnableAliasCheck(enable_flow_sensitive);
        SVF::Options::EnableThreadCallGraph(false);
        SVF::Options::MaxFieldLimit(512);  // 增加字段限制以支持复杂结构体
        
        if (!initializeSVF(modules)) {
            errs() << "Failed to initialize SVF infrastructure\n";
            return false;
        }
        
        if (!runPointerAnalysis()) {
            errs() << "Failed to run SVF pointer analysis\n";
            return false;
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        outs() << "SVF initialization completed in " << duration.count() << " ms\n";
        
        return true;
        
    } catch (const std::exception& e) {
        errs() << "SVF initialization failed with exception: " << e.what() << "\n";
        return false;
    }
#endif
}

#ifdef SVF_AVAILABLE
bool SVFAnalyzer::initializeSVF(const std::vector<std::unique_ptr<Module>>& modules) {
    // 创建SVF模块
    std::vector<std::string> module_names;
    for (const auto& M : modules) {
        module_names.push_back(M->getName().str());
    }
    
    // 构建SVFIR
    SVF::SVFIRBuilder builder(true);
    
    // 添加所有模块到SVF
    for (const auto& M : modules) {
        builder.buildSVFIR(const_cast<Module*>(M.get()));
    }
    
    svfir = std::unique_ptr<SVF::SVFIR>(builder.build());
    if (!svfir) {
        errs() << "Failed to build SVFIR\n";
        return false;
    }
    
    outs() << "SVFIR built successfully with " << svfir->getPAGNodeNum() << " nodes\n";
    
    return true;
}

bool SVFAnalyzer::runPointerAnalysis() {
    if (!svfir) {
        errs() << "SVFIR not initialized\n";
        return false;
    }
    
    // 创建Andersen指针分析
    ander_pta = std::make_unique<SVF::AndersenWaveDiff>(svfir.get());
    ander_pta->analyze();
    
    outs() << "Andersen pointer analysis completed\n";
    
    // 如果启用了流敏感分析
    if (enable_flow_sensitive) {
        flow_pta = std::make_unique<SVF::FlowSensitive>(svfir.get());
        flow_pta->analyze();
        outs() << "Flow-sensitive pointer analysis completed\n";
    }
    
    // 构建VFG (Value Flow Graph)
    vfg = std::make_unique<SVF::VFG>(ander_pta->getCallGraph());
    outs() << "VFG built with " << vfg->getTotalNodeNum() << " nodes\n";
    
    return true;
}
#endif

//===----------------------------------------------------------------------===//
// 函数指针分析
//===----------------------------------------------------------------------===//

std::vector<SVFFunctionPointerResult> SVFAnalyzer::analyzeFunctionPointers(Function* F) {
    std::vector<SVFFunctionPointerResult> results;
    
    if (!F) {
        return results;
    }
    
    outs() << "SVF analyzing function pointers in: " << F->getName() << "\n";
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (!CI->getCalledFunction()) {
                    // 这是一个间接调用
                    SVFFunctionPointerResult result = analyzeFunctionPointerCall(CI);
                    if (!result.possible_targets.empty()) {
                        results.push_back(result);
                    }
                }
            }
        }
    }
    
    outs() << "Found " << results.size() << " function pointer call sites\n";
    return results;
}

SVFFunctionPointerResult SVFAnalyzer::analyzeFunctionPointerCall(CallInst* CI) {
    SVFFunctionPointerResult result;
    result.source_function = CI->getFunction();
    result.call_site = CI;
    
    // 检查缓存
    if (fp_analysis_cache.find(CI) != fp_analysis_cache.end()) {
        return fp_analysis_cache[CI];
    }
    
#ifdef SVF_AVAILABLE
    if (ander_pta) {
        // 使用SVF分析函数指针目标
        result.possible_targets = getFunctionTargets(CI);
        result.analysis_method = "andersen";
        
        // 如果启用了流敏感分析，使用更精确的结果
        if (flow_pta && enable_flow_sensitive) {
            // 流敏感分析可能会给出更精确的结果
            result.analysis_method = "flow_sensitive";
            result.is_precise = true;
        }
        
        // 计算置信度分数
        for (Function* target : result.possible_targets) {
            // 基于SVF分析结果计算置信度
            int confidence = 70; // 基础置信度
            
            // 如果是流敏感分析结果，增加置信度
            if (result.is_precise) {
                confidence += 20;
            }
            
            // 基于函数签名匹配调整置信度
            FunctionType* call_type = CI->getFunctionType();
            FunctionType* target_type = target->getFunctionType();
            
            if (call_type->getReturnType() == target_type->getReturnType() &&
                call_type->getNumParams() == target_type->getNumParams()) {
                confidence += 10;
            }
            
            result.confidence_scores[target] = std::min(confidence, 100);
        }
    }
#endif
    
    // 如果SVF不可用或没有结果，使用基础分析
    if (result.possible_targets.empty()) {
        result.analysis_method = "basic_fallback";
        // 这里可以集成原有的基础函数指针分析逻辑
    }
    
    // 缓存结果
    fp_analysis_cache[CI] = result;
    return result;
}

#ifdef SVF_AVAILABLE
std::vector<Function*> SVFAnalyzer::getFunctionTargets(const SVF::CallInst* cs) {
    std::vector<Function*> targets;
    
    if (!ander_pta) {
        return targets;
    }
    
    // 获取调用点的指针分析信息
    const SVF::CallSite* call_site = SVF::SVFUtil::getLLVMCallSite(cs);
    if (!call_site) {
        return targets;
    }
    
    // 通过SVF获取可能的调用目标
    const SVF::FunctionSet& target_set = ander_pta->getIndirectCallsites().at(call_site);
    
    for (const SVF::SVFFunction* svf_func : target_set) {
        if (Function* llvm_func = const_cast<Function*>(svf_func->getLLVMFun())) {
            targets.push_back(llvm_func);
        }
    }
    
    return targets;
}
#endif

//===----------------------------------------------------------------------===//
// 指针分析
//===----------------------------------------------------------------------===//

std::vector<SVFPointerAnalysisResult> SVFAnalyzer::analyzePointers(Function* F) {
    std::vector<SVFPointerAnalysisResult> results;
    
    if (!F) {
        return results;
    }
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* LI = dyn_cast<LoadInst>(&I)) {
                Value* ptr = LI->getPointerOperand();
                SVFPointerAnalysisResult result = analyzePointer(ptr);
                if (result.precision_score > 50) {  // 只保留高质量结果
                    results.push_back(result);
                }
            } else if (auto* SI = dyn_cast<StoreInst>(&I)) {
                Value* ptr = SI->getPointerOperand();
                SVFPointerAnalysisResult result = analyzePointer(ptr);
                if (result.precision_score > 50) {
                    results.push_back(result);
                }
            }
        }
    }
    
    return results;
}

SVFPointerAnalysisResult SVFAnalyzer::analyzePointer(Value* ptr) {
    SVFPointerAnalysisResult result;
    result.pointer = ptr;
    
    // 检查缓存
    if (pointer_analysis_cache.find(ptr) != pointer_analysis_cache.end()) {
        return pointer_analysis_cache[ptr];
    }
    
#ifdef SVF_AVAILABLE
    if (ander_pta && svfir) {
        // 获取SVF中对应的PAG节点
        const SVF::PAGNode* pag_node = svfir->getPAG()->getGNode(
            SVF::SVFUtil::cast<SVF::PAGNode>(svfir->getDefSVFVar(ptr)));
            
        if (pag_node) {
            // 获取points-to集合
            const SVF::PointsTo& pts = ander_pta->getPts(pag_node->getId());
            
            // 转换为LLVM Value集合
            for (SVF::PointsTo::iterator it = pts.begin(); it != pts.end(); ++it) {
                const SVF::PAGNode* target_node = svfir->getPAG()->getGNode(*it);
                if (Value* target_value = svfNodeToLLVMValue(target_node)) {
                    result.points_to_set.insert(target_value);
                }
            }
            
            // 分析指针类型
            if (isa<GlobalVariable>(ptr)) {
                result.is_global_pointer = true;
            } else if (isa<AllocaInst>(ptr)) {
                result.is_stack_pointer = true;
            }
            
            // 计算精度分数
            result.precision_score = calculatePrecisionScore(result.points_to_set);
            
            // 分析访问的结构体字段
            if (auto* GEP = dyn_cast<GetElementPtrInst>(ptr)) {
                if (auto* struct_type = dyn_cast<StructType>(GEP->getSourceElementType())) {
                    auto struct_fields = analyzeStructType(struct_type);
                    
                    // 找到被访问的特定字段
                    if (GEP->getNumOperands() >= 3) {
                        if (auto* field_idx = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                            unsigned idx = field_idx->getZExtValue();
                            if (idx < struct_fields.size()) {
                                result.accessed_fields.push_back(struct_fields[idx]);
                            }
                        }
                    }
                }
            }
        }
    }
#endif
    
    // 构建描述字符串
    std::string desc = "Pointer to ";
    if (result.is_global_pointer) desc += "global ";
    if (result.is_stack_pointer) desc += "stack ";
    if (result.is_heap_pointer) desc += "heap ";
    desc += "object";
    
    if (!result.points_to_set.empty()) {
        desc += " (points to " + std::to_string(result.points_to_set.size()) + " objects)";
    }
    
    result.pointer_description = desc;
    
    // 缓存结果
    pointer_analysis_cache[ptr] = result;
    return result;
}

#ifdef SVF_AVAILABLE
Value* SVFAnalyzer::svfNodeToLLVMValue(const SVF::PAGNode* node) {
    if (!node) return nullptr;
    
    // 根据SVF节点类型转换为LLVM Value
    if (const SVF::ValVar* val_var = SVF::SVFUtil::dyn_cast<SVF::ValVar>(node)) {
        return const_cast<Value*>(val_var->getValue());
    } else if (const SVF::ObjVar* obj_var = SVF::SVFUtil::dyn_cast<SVF::ObjVar>(node)) {
        const SVF::MemObj* mem_obj = obj_var->getMemObj();
        if (mem_obj->isGlobalObj()) {
            return const_cast<Value*>(mem_obj->getValue());
        }
    }
    
    return nullptr;
}
#endif

//===----------------------------------------------------------------------===//
// 结构体分析
//===----------------------------------------------------------------------===//

std::map<std::string, std::vector<SVFStructFieldInfo>> SVFAnalyzer::analyzeStructUsage(Function* F) {
    std::map<std::string, std::vector<SVFStructFieldInfo>> struct_usage;
    
    if (!F) {
        return struct_usage;
    }
    
    // 收集函数中使用的所有结构体类型
    std::set<StructType*> used_structs;
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* GEP = dyn_cast<GetElementPtrInst>(&I)) {
                if (auto* struct_type = dyn_cast<StructType>(GEP->getSourceElementType())) {
                    used_structs.insert(struct_type);
                }
            }
        }
    }
    
    // 分析每个结构体类型
    for (StructType* ST : used_structs) {
        std::string struct_name = ST->getName().str();
        if (struct_name.empty()) {
            struct_name = "anonymous_struct_" + std::to_string(reinterpret_cast<uintptr_t>(ST));
        }
        
        auto field_info = analyzeStructType(ST);
        if (!field_info.empty()) {
            struct_usage[struct_name] = field_info;
        }
    }
    
    return struct_usage;
}

std::vector<SVFStructFieldInfo> SVFAnalyzer::analyzeStructType(StructType* ST) {
    std::vector<SVFStructFieldInfo> fields;
    
    if (!ST) {
        return fields;
    }
    
    // 检查缓存
    if (struct_info_cache.find(ST) != struct_info_cache.end()) {
        return struct_info_cache[ST];
    }
    
    std::string struct_name = ST->getName().str();
    
    // 分析每个字段
    for (unsigned i = 0; i < ST->getNumElements(); ++i) {
        SVFStructFieldInfo field_info;
        field_info.struct_name = struct_name;
        field_info.field_index = i;
        field_info.field_type = ST->getElementType(i);
        
        // 构建字段名
        field_info.field_name = "field_" + std::to_string(i);
        
        // 检查是否是函数指针字段
        if (auto* ptr_type = dyn_cast<PointerType>(field_info.field_type)) {
            if (ptr_type->getElementType()->isFunctionTy()) {
                field_info.is_function_pointer = true;
                
#ifdef SVF_AVAILABLE
                // 使用SVF分析存储在此字段中的函数
                // 这需要遍历所有对此字段的存储操作
                if (svfir) {
                    // 查找对此结构体字段的所有GEP+Store操作
                    // 这是一个复杂的分析，需要遍历整个SVFIR
                }
#endif
            }
        }
        
        fields.push_back(field_info);
    }
    
    // 缓存结果
    struct_info_cache[ST] = fields;
    return fields;
}

//===----------------------------------------------------------------------===//
// 内存访问模式发现
//===----------------------------------------------------------------------===//

std::vector<SVFMemoryAccessPattern> SVFAnalyzer::discoverAccessPatterns(Function* F) {
    discovered_patterns.clear();
    
    if (F) {
        discoverMemoryAccessPatterns(F);
    }
    
    return discovered_patterns;
}

void SVFAnalyzer::discoverMemoryAccessPatterns(Function* F) {
    // 收集函数中的所有内存访问
    std::vector<Value*> access_sequence;
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (isa<LoadInst>(&I) || isa<StoreInst>(&I)) {
                access_sequence.push_back(&I);
            }
        }
    }
    
    // 分析访问模式
    if (access_sequence.size() >= 2) {
        SVFMemoryAccessPattern pattern;
        pattern.pattern_name = "sequential_access_" + F->getName().str();
        pattern.access_sequence = access_sequence;
        pattern.frequency = access_sequence.size();
        
        // 检查是否是设备访问模式
        pattern.is_device_access_pattern = isDeviceAccessPattern(access_sequence);
        
        // 检查是否涉及内核数据结构
        pattern.is_kernel_data_structure = false;
        for (Value* access : access_sequence) {
            if (isKernelDataStructureAccess(access)) {
                pattern.is_kernel_data_structure = true;
                break;
            }
        }
        
        discovered_patterns.push_back(pattern);
    }
}

bool SVFAnalyzer::isDeviceAccessPattern(const std::vector<Value*>& access_seq) {
    // 启发式判断：如果访问序列中包含通过dev_id参数的访问
    for (Value* access : access_seq) {
        if (auto* LI = dyn_cast<LoadInst>(access)) {
            Value* ptr = LI->getPointerOperand();
            if (auto* GEP = dyn_cast<GetElementPtrInst>(ptr)) {
                Value* base = GEP->getPointerOperand();
                if (auto* arg = dyn_cast<Argument>(base)) {
                    Function* F = arg->getParent();
                    if (F && F->arg_size() == 2 && arg->getArgNo() == 1) {
                        // 这是IRQ处理函数的dev_id参数访问
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool SVFAnalyzer::isKernelDataStructureAccess(Value* ptr) {
    // 检查是否访问已知的内核数据结构
    if (auto* GEP = dyn_cast<GetElementPtrInst>(ptr)) {
        if (auto* struct_type = dyn_cast<StructType>(GEP->getSourceElementType())) {
            std::string struct_name = struct_type->getName().str();
            
            // 常见的内核数据结构
            static const std::vector<std::string> kernel_structs = {
                "struct.pci_dev", "struct.device", "struct.irq_desc",
                "struct.task_struct", "struct.file", "struct.inode",
                "struct.net_device", "struct.sk_buff", "struct.work_struct"
            };
            
            for (const auto& kernel_struct : kernel_structs) {
                if (struct_name.find(kernel_struct) != std::string::npos) {
                    return true;
                }
            }
        }
    }
    return false;
}

int SVFAnalyzer::calculatePrecisionScore(const std::set<Value*>& points_to_set) {
    if (points_to_set.empty()) {
        return 0;
    }
    
    // 计算精度分数：points-to集合越小，精度越高
    int base_score = 100 - std::min(static_cast<int>(points_to_set.size()) * 10, 80);
    
    // 根据points-to目标的类型调整分数
    for (Value* target : points_to_set) {
        if (isa<GlobalVariable>(target)) {
            base_score += 5;  // 全局变量增加置信度
        } else if (isa<AllocaInst>(target)) {
            base_score += 3;  // 栈变量增加置信度
        }
    }
    
    return std::min(base_score, 100);
}

//===----------------------------------------------------------------------===//
// 增强现有分析
//===----------------------------------------------------------------------===//

MemoryAccessInfo SVFAnalyzer::enhanceMemoryAccessInfo(const MemoryAccessInfo& original, Value* ptr) {
    MemoryAccessInfo enhanced = original;
    
    if (!ptr) {
        return enhanced;
    }
    
    // 使用SVF分析增强内存访问信息
    SVFPointerAnalysisResult svf_result = analyzePointer(ptr);
    
    if (svf_result.precision_score > enhanced.confidence) {
        enhanced.confidence = svf_result.precision_score;
        enhanced.chain_description += " (SVF enhanced)";
        
        // 如果SVF发现了更精确的信息，更新访问类型
        if (svf_result.is_global_pointer && enhanced.type == MemoryAccessInfo::INDIRECT_ACCESS) {
            enhanced.type = MemoryAccessInfo::GLOBAL_VARIABLE;
        }
        
        // 添加结构体字段信息
        if (!svf_result.accessed_fields.empty()) {
            const auto& field = svf_result.accessed_fields[0];
            enhanced.struct_type_name = field.struct_name;
            enhanced.chain_description += " -> " + field.field_name;
            
            if (field.is_function_pointer) {
                enhanced.chain_description += " (function pointer field)";
            }
        }
    }
    
    return enhanced;
}

std::vector<FunctionPointerTarget> SVFAnalyzer::enhanceFunctionPointerAnalysis(CallInst* CI) {
    std::vector<FunctionPointerTarget> targets;
    
    if (!CI) {
        return targets;
    }
    
    SVFFunctionPointerResult svf_result = analyzeFunctionPointerCall(CI);
    
    // 转换SVF结果为标准格式
    for (Function* target : svf_result.possible_targets) {
        int confidence = 50;  // 默认置信度
        
        auto conf_it = svf_result.confidence_scores.find(target);
        if (conf_it != svf_result.confidence_scores.end()) {
            confidence = conf_it->second;
        }
        
        std::string reason = "SVF " + svf_result.analysis_method;
        if (svf_result.is_precise) {
            reason += " (precise)";
        }
        
        targets.emplace_back(target, confidence, reason);
    }
    
    return targets;
}

//===----------------------------------------------------------------------===//
// 统计和调试
//===----------------------------------------------------------------------===//

void SVFAnalyzer::printStatistics() const {
    outs() << "\n=== SVF Analysis Statistics ===\n";
    outs() << "SVF Available: " << (isSVFAvailable() ? "Yes" : "No") << "\n";
    
    if (isSVFAvailable()) {
        outs() << "SVF Version: " << getSVFVersion() << "\n";
        
#ifdef SVF_AVAILABLE
        if (svfir) {
            outs() << "SVFIR Nodes: " << svfir->getPAGNodeNum() << "\n";
        }
        
        if (ander_pta) {
            outs() << "Andersen PTA: Enabled\n";
        }
        
        if (flow_pta) {
            outs() << "Flow-sensitive PTA: Enabled\n";
        }
        
        if (vfg) {
            outs() << "VFG Nodes: " << vfg->getTotalNodeNum() << "\n";
        }
#endif
    }
    
    outs() << "Function Pointer Analyses: " << fp_analysis_cache.size() << "\n";
    outs() << "Pointer Analyses: " << pointer_analysis_cache.size() << "\n";
    outs() << "Struct Type Analyses: " << struct_info_cache.size() << "\n";
    outs() << "Discovered Patterns: " << discovered_patterns.size() << "\n";
}

void SVFAnalyzer::clearCache() {
    fp_analysis_cache.clear();
    pointer_analysis_cache.clear();
    struct_info_cache.clear();
    discovered_patterns.clear();
}

//===----------------------------------------------------------------------===//
// SVF集成助手实现
//===----------------------------------------------------------------------===//

bool SVFIntegrationHelper::initialize(const std::vector<std::unique_ptr<Module>>& modules) {
    if (!SVFAnalyzer::isSVFAvailable()) {
        errs() << "Warning: SVF is not available, using fallback analysis\n";
        svf_available = false;
        return false;  // 不是错误，只是功能降级
    }
    
    svf_analyzer = new SVFAnalyzer();
    if (!svf_analyzer) {
        errs() << "Failed to create SVF analyzer\n";
        return false;
    }
    
    if (!svf_analyzer->initialize(modules)) {
        delete svf_analyzer;
        svf_analyzer = nullptr;
        errs() << "Failed to initialize SVF analyzer\n";
        return false;
    }
    
    svf_available = true;
    outs() << "SVF integration successfully initialized\n";
    return true;
}

MemoryAccessInfo SVFIntegrationHelper::createEnhancedMemoryAccess(const MemoryAccessInfo& original, Value* ptr) {
    if (!isInitialized()) {
        return original;  // 返回原始分析结果
    }
    
    return svf_analyzer->enhanceMemoryAccessInfo(original, ptr);
}

std::vector<FunctionPointerTarget> SVFIntegrationHelper::createEnhancedFunctionPointerTargets(CallInst* CI) {
    std::vector<FunctionPointerTarget> targets;
    
    if (!isInitialized()) {
        return targets;  // 返回空结果，调用者会使用基础分析
    }
    
    return svf_analyzer->enhanceFunctionPointerAnalysis(CI);
}

void SVFIntegrationHelper::printStatus() const {
    outs() << "=== SVF Integration Status ===\n";
    outs() << "SVF Available: " << SVFAnalyzer::isSVFAvailable() << "\n";
    outs() << "SVF Initialized: " << isInitialized() << "\n";
    
    if (isInitialized()) {
        outs() << "SVF Version: " << SVFAnalyzer::getSVFVersion() << "\n";
        svf_analyzer->printStatistics();
    } else {
        outs() << "Using fallback analysis methods\n";
    }
}
