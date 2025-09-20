//===- SVFIntegration.cpp - SVF集成到现有分析器中 ------------------------===//

#include "CrossModuleAnalyzer.h"

#ifdef ENABLE_SVF
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "Graphs/SVFG.h"
#include "WPA/Andersen.h"
#include "WPA/FlowSensitive.h"
#include "Util/Options.h"
#include "MemoryModel/PointerAnalysis.h"

using namespace SVF;
#endif

#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// SVF增强的函数指针分析器
//===----------------------------------------------------------------------===//

#ifdef ENABLE_SVF

class SVFEnhancedFunctionPointerAnalyzer {
private:
    std::unique_ptr<SVFModule> svf_module;
    std::unique_ptr<SVFIR> svf_ir;
    std::unique_ptr<AndersenWaveDiff> andersen_pta;
    
public:
    bool initialize(const std::vector<std::unique_ptr<Module>>& modules) {
        outs() << "Initializing SVF for function pointer analysis...\n";
        
        // 设置SVF选项
        Options::PAGPrint.setValue(false);
        Options::AndersenDotGraph.setValue(false);
        Options::PrintAliases.setValue(false);
        
        // 构建SVF模块
        std::vector<const Module*> module_ptrs;
        for (const auto& mod : modules) {
            module_ptrs.push_back(mod.get());
        }
        
        svf_module = LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(module_ptrs);
        if (!svf_module) {
            errs() << "Failed to create SVF module\n";
            return false;
        }
        
        // 构建SVF IR
        SVFIRBuilder builder(svf_module.get());
        svf_ir = std::unique_ptr<SVFIR>(builder.build());
        if (!svf_ir) {
            errs() << "Failed to build SVF IR\n";
            return false;
        }
        
        // 运行Andersen指针分析
        andersen_pta = std::make_unique<AndersenWaveDiff>(svf_ir.get());
        andersen_pta->analyze();
        
        outs() << "SVF initialization completed:\n";
        outs() << "  SVF IR nodes: " << svf_ir->getTotalNodeNum() << "\n";
        outs() << "  Resolved indirect calls: " << andersen_pta->numOfResolvedIndCallEdge() << "\n";
        
        return true;
    }
    
    std::vector<FunctionPointerCandidate> analyzeIndirectCall(CallInst* call_inst) {
        std::vector<FunctionPointerCandidate> candidates;
        
        if (!svf_ir || !andersen_pta) {
            return candidates;
        }
        
        const SVFInstruction* svf_call = svf_module->getSVFInstruction(call_inst);
        if (!svf_call) {
            return candidates;
        }
        
        // 获取被调用的函数指针
        const SVFValue* callee = svf_call->getCalledOperand();
        if (!callee) {
            return candidates;
        }
        
        // 获取函数指针的points-to集合
        NodeID pointer_id = svf_ir->getValueNode(callee);
        if (pointer_id == UNDEF_ID) {
            return candidates;
        }
        
        const PointsTo& pts = andersen_pta->getPts(pointer_id);
        
        // 转换points-to集合为候选函数
        for (NodeID obj_id : pts) {
            const PAGNode* obj_node = svf_ir->getGNode(obj_id);
            if (!obj_node) continue;
            
            if (const SVFFunction* svf_func = SVFUtil::dyn_cast<SVFFunction>(obj_node->getValue())) {
                Function* llvm_func = const_cast<Function*>(svf_func->getLLVMFun());
                if (llvm_func) {
                    int confidence = 85; // SVF提供的高置信度
                    std::string reason = "SVF_Andersen_analysis";
                    std::string module_name = llvm_func->getParent()->getName().str();
                    
                    candidates.emplace_back(llvm_func, confidence, reason, module_name, SymbolScope::GLOBAL);
                }
            }
        }
        
        return candidates;
    }
    
    std::set<Value*> getPointsToSet(Value* pointer) {
        std::set<Value*> result;
        
        if (!svf_ir || !andersen_pta || !pointer) {
            return result;
        }
        
        const SVFValue* svf_ptr = svf_module->getSVFValue(pointer);
        if (!svf_ptr) {
            return result;
        }
        
        NodeID ptr_id = svf_ir->getValueNode(svf_ptr);
        if (ptr_id == UNDEF_ID) {
            return result;
        }
        
        const PointsTo& pts = andersen_pta->getPts(ptr_id);
        
        for (NodeID obj_id : pts) {
            const PAGNode* obj_node = svf_ir->getGNode(obj_id);
            if (!obj_node) continue;
            
            const SVFValue* obj_value = obj_node->getValue();
            if (!obj_value) continue;
            
            Value* llvm_value = const_cast<Value*>(obj_value->getValue());
            if (llvm_value) {
                result.insert(llvm_value);
            }
        }
        
        return result;
    }
};

#endif // ENABLE_SVF

//===----------------------------------------------------------------------===//
// CrossModuleAnalyzer的SVF集成修改
//===----------------------------------------------------------------------===//

void CrossModuleAnalyzer::createSpecializedAnalyzers() {
    // 设置数据布局（使用第一个模块的）
    if (!modules.empty()) {
        data_layout = std::make_unique<DataLayout>(modules[0]->getDataLayout());
    }
    
    // 创建专门的分析器
    dataflow_analyzer = std::make_unique<DataFlowAnalyzer>(&enhanced_symbols);
    deep_fp_analyzer = std::make_unique<DeepFunctionPointerAnalyzer>(&enhanced_symbols, dataflow_analyzer.get());
    memory_analyzer = std::make_unique<EnhancedCrossModuleMemoryAnalyzer>(this, dataflow_analyzer.get(), data_layout.get());
    asm_analyzer = std::make_unique<InlineAsmAnalyzer>();
    
#ifdef ENABLE_SVF
    // 如果启用SVF，初始化SVF分析器
    if (enable_svf_analysis) {
        outs() << "Initializing SVF-enhanced analysis...\n";
        svf_analyzer = std::make_unique<SVFEnhancedFunctionPointerAnalyzer>();
        if (!svf_analyzer->initialize(modules)) {
            errs() << "Warning: SVF initialization failed, falling back to standard analysis\n";
            svf_analyzer.reset();
            enable_svf_analysis = false;
        } else {
            outs() << "SVF analysis enabled successfully\n";
        }
    }
#endif
}

std::vector<FunctionCallInfo> CrossModuleAnalyzer::analyzeHandlerFunctionCalls(Function* F) {
    std::vector<FunctionCallInfo> calls;
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (Function* callee = CI->getCalledFunction()) {
                    // 直接函数调用 - 过滤LLVM内置函数
                    std::string callee_name = callee->getName().str();
                    
                    // 跳过LLVM内置函数和编译器生成的函数
                    if (shouldFilterFunction(callee_name)) {
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
                    
                    calls.push_back(info);
                    
                } else {
                    // 间接函数调用
                    if (!isActualIndirectCall(CI)) {
                        continue;
                    }
                    
                    std::vector<FunctionPointerCandidate> candidates;
                    
#ifdef ENABLE_SVF
                    // 优先使用SVF分析
                    if (enable_svf_analysis && svf_analyzer) {
                        candidates = svf_analyzer->analyzeIndirectCall(CI);
                        
                        if (!candidates.empty()) {
                            errs() << "SVF found " << candidates.size() << " candidates for indirect call\n";
                        }
                    }
#endif
                    
                    // 如果SVF没有找到结果，使用原有的深度分析
                    if (candidates.empty()) {
                        candidates = deep_fp_analyzer->analyzeDeep(CI->getCalledOperand());
                    }
                    
                    // 转换候选函数为函数调用信息
                    for (const auto& candidate : candidates) {
                        std::string candidate_name = candidate.function->getName().str();
                        
                        // 跳过LLVM内置函数
                        if (shouldFilterFunction(candidate_name)) {
                            continue;
                        }
                        
                        FunctionCallInfo info;
                        info.callee_name = candidate_name;
                        info.is_direct_call = false;
                        info.confidence = candidate.confidence;
                        
#ifdef ENABLE_SVF
                        if (enable_svf_analysis && svf_analyzer && !candidates.empty()) {
                            info.analysis_reason = "SVF_" + candidate.match_reason;
                        } else {
                            info.analysis_reason = candidate.match_reason;
                        }
#else
                        info.analysis_reason = candidate.match_reason;
#endif
                        
                        calls.push_back(info);
                    }
                }
            }
        }
    }
    
    return calls;
}

// 辅助函数：检查是否应该过滤函数
static bool shouldFilterFunction(const std::string& name) {
    // LLVM内置函数
    if (name.find("llvm.") == 0) {
        return true;
    }
    
    // 编译器插桩函数
    static const std::vector<std::string> filter_prefixes = {
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
        "__profile_",
        "__stack_chk_"
    };
    
    for (const auto& prefix : filter_prefixes) {
        if (name.find(prefix) == 0) {
            return true;
        }
    }
    
    return false;
}

// 辅助函数：检查是否为真正的间接调用
static bool isActualIndirectCall(CallInst* CI) {
    if (CI->getCalledFunction()) {
        return false; // 有直接目标，不是间接调用
    }
    
    Value* callee = CI->getCalledOperand();
    
    // 检查是否是内联汇编
    if (isa<InlineAsm>(callee)) {
        return false;
    }
    
    // 检查是否是常量表达式（可能是直接调用的变形）
    if (isa<ConstantExpr>(callee)) {
        return false;
    }
    
    // 检查是否是直接的函数指针常量
    if (isa<Function>(callee)) {
        return false;
    }
    
    // 只有通过寄存器或内存加载的函数指针才是真正的间接调用
    return isa<LoadInst>(callee) || isa<Argument>(callee) || isa<PHINode>(callee);
}
