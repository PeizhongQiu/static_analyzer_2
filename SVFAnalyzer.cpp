//===- SVFAnalyzer.cpp - SVF分析器核心实现 -----------------------------------===//

#include "SVFAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/MemoryBuffer.h"
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

//===----------------------------------------------------------------------===//
// SVF初始化
//===----------------------------------------------------------------------===//

bool SVFAnalyzer::initialize(const std::vector<std::unique_ptr<Module>>& modules) {
#ifndef SVF_AVAILABLE
    errs() << "Error: SVF not available. Please install SVF and rebuild.\n";
    return false;
#else
    if (modules.empty()) {
        errs() << "Error: No modules provided for SVF analysis\n";
        return false;
    }
    
    outs() << "Initializing SVF analysis...\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // 设置SVF选项
        SVF::Options::EnableAliasCheck(enable_flow_sensitive);
        SVF::Options::EnableThreadCallGraph(false);
        SVF::Options::MaxFieldLimit(512);
        
        if (!initializeSVF(modules)) {
            return false;
        }
        
        if (!runPointerAnalysis()) {
            return false;
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        outs() << "SVF initialization completed in " << duration.count() << " ms\n";
        return true;
        
    } catch (const std::exception& e) {
        errs() << "SVF initialization failed: " << e.what() << "\n";
        return false;
    }
#endif
}

#ifdef SVF_AVAILABLE
bool SVFAnalyzer::initializeSVF(const std::vector<std::unique_ptr<Module>>& modules) {
    // 构建SVFIR
    SVF::SVFIRBuilder builder(true);
    
    for (const auto& M : modules) {
        builder.buildSVFIR(const_cast<Module*>(M.get()));
    }
    
    svfir = std::unique_ptr<SVF::SVFIR>(builder.build());
    if (!svfir) {
        errs() << "Failed to build SVFIR\n";
        return false;
    }
    
    outs() << "SVFIR built with " << svfir->getPAGNodeNum() << " nodes\n";
    return true;
}

bool SVFAnalyzer::runPointerAnalysis() {
    if (!svfir) {
        return false;
    }
    
    // Andersen指针分析
    ander_pta = std::make_unique<SVF::AndersenWaveDiff>(svfir.get());
    ander_pta->analyze();
    
    outs() << "Andersen pointer analysis completed\n";
    
    // 流敏感分析（可选）
    if (enable_flow_sensitive) {
        flow_pta = std::make_unique<SVF::FlowSensitive>(svfir.get());
        flow_pta->analyze();
        outs() << "Flow-sensitive analysis completed\n";
    }
    
    // 构建VFG
    vfg = std::make_unique<SVF::VFG>(ander_pta->getCallGraph());
    outs() << "VFG built with " << vfg->getTotalNodeNum() << " nodes\n";
    
    return true;
}
#endif

//===----------------------------------------------------------------------===//
// 中断处理函数分析
//===----------------------------------------------------------------------===//

SVFInterruptHandlerAnalysis SVFAnalyzer::analyzeHandler(Function* handler) {
    SVFInterruptHandlerAnalysis analysis;
    
    if (!handler) {
        return analysis;
    }
    
    analysis.function_name = handler->getName().str();
    
    // 获取源文件信息
    if (auto* SP = handler->getSubprogram()) {
        analysis.source_file = SP->getFilename().str();
    }
    
    outs() << "Analyzing handler: " << analysis.function_name << "\n";
    
    // 1. 分析函数指针调用
    for (auto& BB : *handler) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (!CI->getCalledFunction()) {
                    // 间接调用
                    SVFFunctionPointerResult fp_result = analyzeFunctionPointer(CI);
                    if (!fp_result.possible_targets.empty()) {
                        analysis.function_pointer_calls.push_back(fp_result);
                    }
                }
            }
        }
    }
    
    // 2. 分析结构体使用
    analysis.struct_usage = analyzeStructUsage(handler);
    
    // 3. 发现访问模式
    analysis.access_patterns = discoverAccessPatterns(handler);
    
    // 4. 分析指向的对象
    for (auto& BB : *handler) {
        for (auto& I : BB) {
            if (auto* LI = dyn_cast<LoadInst>(&I)) {
                Value* ptr = LI->getPointerOperand();
                auto points_to = getPointsToSet(ptr);
                analysis.pointed_objects.insert(points_to.begin(), points_to.end());
            }
        }
    }
    
    // 5. 计算精度分数
    analysis.svf_precision_score = calculatePrecisionScore(analysis);
    analysis.svf_analysis_complete = true;
    
    outs() << "  Function pointers: " << analysis.function_pointer_calls.size() << "\n";
    outs() << "  Struct types: " << analysis.struct_usage.size() << "\n";
    outs() << "  Access patterns: " << analysis.access_patterns.size() << "\n";
    outs() << "  Precision score: " << analysis.svf_precision_score << "\n";
    
    return analysis;
}

//===----------------------------------------------------------------------===//
// 函数指针分析
//===----------------------------------------------------------------------===//

SVFFunctionPointerResult SVFAnalyzer::analyzeFunctionPointer(CallInst* call) {
    SVFFunctionPointerResult result;
    result.call_site = call;
    result.source_function = call->getFunction();
    
    // 检查缓存
    if (fp_cache.find(call) != fp_cache.end()) {
        return fp_cache[call];
    }
    
#ifdef SVF_AVAILABLE
    if (ander_pta) {
        result.possible_targets = getFunctionTargets(call);
        result.analysis_method = "andersen";
        
        if (flow_pta && enable_flow_sensitive) {
            result.analysis_method = "flow_sensitive";
            result.is_precise = true;
        }
        
        // 计算置信度
        for (Function* target : result.possible_targets) {
            int confidence = 70;
            
            if (result.is_precise) {
                confidence += 20;
            }
            
            // 基于函数签名匹配
            FunctionType* call_type = call->getFunctionType();
            FunctionType* target_type = target->getFunctionType();
            
            if (call_type->getReturnType() == target_type->getReturnType() &&
                call_type->getNumParams() == target_type->getNumParams()) {
                confidence += 10;
            }
            
            result.confidence_scores[target] = std::min(confidence, 100);
        }
    }
#endif
    
    fp_cache[call] = result;
    return result;
}

#ifdef SVF_AVAILABLE
std::vector<Function*> SVFAnalyzer::getFunctionTargets(CallInst* call) {
    std::vector<Function*> targets;
    
    if (!ander_pta || !svfir) {
        return targets;
    }
    
    // 获取被调用的函数指针
    Value* callee = call->getCalledOperand();
    const SVF::SVFValue* svf_callee = svfir->getSVFValue(callee);
    if (!svf_callee) {
        return targets;
    }
    
    // 获取指针的points-to集合
    SVF::NodeID pointer_id = svfir->getValueNode(svf_callee);
    if (pointer_id == SVF::UNDEF_ID) {
        return targets;
    }
    
    const SVF::PointsTo& pts = ander_pta->getPts(pointer_id);
    
    // 转换为Function*
    for (SVF::NodeID obj_id : pts) {
        const SVF::PAGNode* obj_node = svfir->getGNode(obj_id);
        if (!obj_node) continue;
        
        if (const SVF::SVFFunction* svf_func = SVF::SVFUtil::dyn_cast<SVF::SVFFunction>(obj_node->getValue())) {
            Function* llvm_func = const_cast<Function*>(svf_func->getLLVMFun());
            if (llvm_func) {
                targets.push_back(llvm_func);
            }
        }
    }
    
    return targets;
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
    
    // 收集函数中使用的结构体类型
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
    if (struct_cache.find(ST) != struct_cache.end()) {
        return struct_cache[ST];
    }
    
    std::string struct_name = ST->getName().str();
    
    // 分析每个字段
    for (unsigned i = 0; i < ST->getNumElements(); ++i) {
        SVFStructFieldInfo field_info;
        field_info.struct_name = struct_name;
        field_info.field_index = i;
        field_info.field_type = ST->getElementType(i);
        field_info.field_name = "field_" + std::to_string(i);
        
        // 检查是否是函数指针字段
        if (auto* ptr_type = dyn_cast<PointerType>(field_info.field_type)) {
            if (ptr_type->getElementType()->isFunctionTy()) {
                field_info.is_function_pointer = true;
                
                // 使用SVF分析存储在此字段中的函数
                // 这里可以进一步分析哪些函数被存储到这个字段中
            }
        }
        
        fields.push_back(field_info);
    }
    
    struct_cache[ST] = fields;
    return fields;
}

//===----------------------------------------------------------------------===//
// 内存访问模式发现
//===----------------------------------------------------------------------===//

std::vector<SVFMemoryAccessPattern> SVFAnalyzer::discoverAccessPatterns(Function* F) {
    std::vector<SVFMemoryAccessPattern> patterns;
    
    if (!F) {
        return patterns;
    }
    
    // 收集内存访问序列
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
        pattern.is_device_access_pattern = isDeviceAccessPattern(access_sequence);
        pattern.is_kernel_data_structure = false;
        
        for (Value* access : access_sequence) {
            if (isKernelDataStructureAccess(access)) {
                pattern.is_kernel_data_structure = true;
                break;
            }
        }
        
        patterns.push_back(pattern);
    }
    
    return patterns;
}

bool SVFAnalyzer::isDeviceAccessPattern(const std::vector<Value*>& access_seq) {
    // 检查是否通过dev_id参数访问
    for (Value* access : access_seq) {
        if (auto* LI = dyn_cast<LoadInst>(access)) {
            Value* ptr = LI->getPointerOperand();
            if (auto* GEP = dyn_cast<GetElementPtrInst>(ptr)) {
                Value* base = GEP->getPointerOperand();
                if (auto* arg = dyn_cast<Argument>(base)) {
                    Function* F = arg->getParent();
                    if (F && F->arg_size() == 2 && arg->getArgNo() == 1) {
                        return true; // IRQ处理函数的dev_id参数
                    }
                }
            }
        }
    }
    return false;
}

bool SVFAnalyzer::isKernelDataStructureAccess(Value* ptr) {
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

//===----------------------------------------------------------------------===//
// 指针分析
//===----------------------------------------------------------------------===//

std::set<Value*> SVFAnalyzer::getPointsToSet(Value* pointer) {
    std::set<Value*> result;
    
#ifdef SVF_AVAILABLE
    if (!ander_pta || !svfir || !pointer) {
        return result;
    }
    
    const SVF::SVFValue* svf_ptr = svfir->getSVFValue(pointer);
    if (!svf_ptr) {
        return result;
    }
    
    SVF::NodeID ptr_id = svfir->getValueNode(svf_ptr);
    if (ptr_id == SVF::UNDEF_ID) {
        return result;
    }
    
    const SVF::PointsTo& pts = ander_pta->getPts(ptr_id);
    
    for (SVF::NodeID obj_id : pts) {
        const SVF::PAGNode* obj_node = svfir->getGNode(obj_id);
        if (!obj_node) continue;
        
        Value* llvm_value = svfNodeToLLVMValue(obj_node);
        if (llvm_value) {
            result.insert(llvm_value);
        }
    }
#endif
    
    return result;
}

#ifdef SVF_AVAILABLE
Value* SVFAnalyzer::svfNodeToLLVMValue(const SVF::PAGNode* node) {
    if (!node) return nullptr;
    
    if (const SVF::ValVar* val_var = SVF::SVFUtil::dyn_cast<SVF::ValVar>(node)) {
        return const_cast<Value*>(val_var->getValue()->getValue());
    } else if (const SVF::ObjVar* obj_var = SVF::SVFUtil::dyn_cast<SVF::ObjVar>(node)) {
        const SVF::MemObj* mem_obj = obj_var->getMemObj();
        if (mem_obj->isGlobalObj()) {
            return const_cast<Value*>(mem_obj->getValue()->getValue());
        }
    }
    
    return nullptr;
}
#endif

//===----------------------------------------------------------------------===//
// 分析质量评估
//===----------------------------------------------------------------------===//

double SVFAnalyzer::calculatePrecisionScore(const SVFInterruptHandlerAnalysis& analysis) {
    double total_score = 0.0;
    int scored_items = 0;
    
    // 函数指针分析精度
    for (const auto& fp_result : analysis.function_pointer_calls) {
        if (fp_result.is_precise) {
            total_score += 90.0;
        } else {
            total_score += 70.0;
        }
        scored_items++;
    }
    
    // 结构体分析精度
    for (const auto& struct_pair : analysis.struct_usage) {
        total_score += 80.0;
        scored_items++;
    }
    
    // 访问模式精度
    for (const auto& pattern : analysis.access_patterns) {
        if (pattern.is_device_access_pattern || pattern.is_kernel_data_structure) {
            total_score += 85.0;
        } else {
            total_score += 60.0;
        }
        scored_items++;
    }
    
    return scored_items > 0 ? total_score / scored_items : 0.0;
}

//===----------------------------------------------------------------------===//
// 统计和调试
//===----------------------------------------------------------------------===//

void SVFAnalyzer::printStatistics() const {
    outs() << "\n=== SVF Analysis Statistics ===\n";
    outs() << "SVF Available: " << (isSVFAvailable() ? "Yes" : "No") << "\n";
    
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
    
    outs() << "Function Pointer Analyses: " << fp_cache.size() << "\n";
    outs() << "Struct Type Analyses: " << struct_cache.size() << "\n";
}

void SVFAnalyzer::clearCache() {
    fp_cache.clear();
    struct_cache.clear();
}

//===----------------------------------------------------------------------===//
// SVFIRQAnalyzer 实现
//===----------------------------------------------------------------------===//

bool SVFIRQAnalyzer::loadModules(const std::vector<std::string>& bc_files) {
    if (!context) {
        errs() << "Error: No LLVM context provided\n";
        return false;
    }
    
    modules.clear();
    
    outs() << "Loading " << bc_files.size() << " bitcode files...\n";
    
    size_t loaded = 0;
    for (const auto& bc_file : bc_files) {
        auto BufferOrErr = MemoryBuffer::getFile(bc_file);
        if (std::error_code EC = BufferOrErr.getError()) {
            errs() << "Error reading " << bc_file << ": " << EC.message() << "\n";
            continue;
        }
        
        auto ModuleOrErr = parseBitcodeFile(BufferOrErr.get()->getMemBufferRef(), *context);
        if (!ModuleOrErr) {
            errs() << "Error parsing " << bc_file << ": " << toString(ModuleOrErr.takeError()) << "\n";
            continue;
        }
        
        auto M = std::move(ModuleOrErr.get());
        M->setModuleIdentifier(bc_file);
        modules.push_back(std::move(M));
        loaded++;
    }
    
    outs() << "Loaded " << loaded << "/" << bc_files.size() << " modules\n";
    
    if (loaded == 0) {
        return false;
    }
    
    // 初始化SVF分析器
    svf_analyzer = std::make_unique<SVFAnalyzer>();
    if (!svf_analyzer->initialize(modules)) {
        errs() << "Failed to initialize SVF analyzer\n";
        return false;
    }
    
    return true;
}

std::vector<SVFInterruptHandlerAnalysis> SVFIRQAnalyzer::analyzeAllHandlers(const std::vector<std::string>& handler_names) {
    std::vector<SVFInterruptHandlerAnalysis> results;
    
    if (!isInitialized()) {
        errs() << "SVF analyzer not initialized\n";
        return results;
    }
    
    // 在所有模块中查找处理函数
    std::vector<Function*> found_handlers;
    
    for (const auto& handler_name : handler_names) {
        for (auto& M : modules) {
            for (auto& F : *M) {
                if (F.getName() == handler_name) {
                    found_handlers.push_back(&F);
                    outs() << "Found handler: " << handler_name << " in " << M->getName() << "\n";
                    break;
                }
            }
        }
    }
    
    if (found_handlers.empty()) {
        outs() << "No interrupt handlers found\n";
        return results;
    }
    
    outs() << "\nAnalyzing " << found_handlers.size() << " handlers with SVF...\n";
    
    // 分析每个处理函数
    for (Function* handler : found_handlers) {
        SVFInterruptHandlerAnalysis analysis = svf_analyzer->analyzeHandler(handler);
        results.push_back(analysis);
    }
    
    return results;
}
