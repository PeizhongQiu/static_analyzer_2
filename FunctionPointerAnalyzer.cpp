//===- FunctionPointerAnalyzer.cpp - 简化的函数指针分析器 ----------------===//

#include "SVFInterruptAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"

#ifdef SVF_AVAILABLE
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/LLVMModule.h"
#endif

using namespace llvm;

//===----------------------------------------------------------------------===//
// 核心函数指针解析
//===----------------------------------------------------------------------===//

std::vector<std::string> SVFInterruptAnalyzer::resolveFunctionPointer(Value* func_ptr) {
    std::vector<std::string> targets;

    if (!func_ptr) {
        return targets;
    }
    
#ifdef SVF_AVAILABLE
    if (pta && svfir) {
        SVF::LLVMModuleSet* moduleSet = SVF::LLVMModuleSet::getLLVMModuleSet();
        
        if (moduleSet->hasValueNode(func_ptr)) {
            outs() << "\nIndirect call find \n";
            SVF::NodeID nodeId = moduleSet->getValueNode(func_ptr);
            const SVF::PointsTo& pts = pta->getPts(nodeId);
            
            for (auto ptd : pts) {
                const SVF::PAGNode* targetNode = svfir->getGNode(ptd);
                if (const SVF::FunValVar* funVar = SVF::SVFUtil::dyn_cast<SVF::FunValVar>(targetNode)) {
                    targets.push_back(funVar->getFunction()->getName());
                }
            }
        }
    }
#endif
    
    // 如果SVF无法解析，使用启发式方法
    if (targets.empty()) {
        targets = resolveWithHeuristics(func_ptr);
    }
    
    return targets;
}

std::vector<std::string> SVFInterruptAnalyzer::resolveWithHeuristics(Value* func_ptr) {
    std::vector<std::string> targets;
    
    if (auto* func = dyn_cast<Function>(func_ptr)) {
        // 直接函数引用
        targets.push_back(func->getName().str());
        
    } else if (auto* arg = dyn_cast<Argument>(func_ptr)) {
        // 函数参数
        targets.push_back("function_arg_" + std::to_string(arg->getArgNo()));
        
    } else if (auto* gv = dyn_cast<GlobalVariable>(func_ptr)) {
        // 全局函数指针变量
        std::string name = "global_func_" + gv->getName().str();
        
        // 尝试分析全局变量的初始化器
        if (gv->hasInitializer()) {
            if (auto* init_func = dyn_cast<Function>(gv->getInitializer())) {
                targets.push_back(init_func->getName().str());
            } else {
                targets.push_back(name);
            }
        } else {
            targets.push_back(name);
        }
        
    } else if (auto* load = dyn_cast<LoadInst>(func_ptr)) {
        // 从内存加载的函数指针
        Value* ptr = load->getPointerOperand();
        if (auto* gep = dyn_cast<GetElementPtrInst>(ptr)) {
            targets.push_back("struct_field_function_" + analyzeGEPFieldAccess(gep));
        } else if (auto* gv = dyn_cast<GlobalVariable>(ptr)) {
            targets.push_back("loaded_global_func_" + gv->getName().str());
        } else {
            targets.push_back("loaded_function_pointer");
        }
        
    } else if (auto* select = dyn_cast<SelectInst>(func_ptr)) {
        // 条件选择的函数指针
        auto true_targets = resolveWithHeuristics(select->getTrueValue());
        auto false_targets = resolveWithHeuristics(select->getFalseValue());
        targets.insert(targets.end(), true_targets.begin(), true_targets.end());
        targets.insert(targets.end(), false_targets.begin(), false_targets.end());
        
    } else if (auto* phi = dyn_cast<PHINode>(func_ptr)) {
        // PHI节点 - 来自不同基本块的函数指针
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
            auto phi_targets = resolveWithHeuristics(phi->getIncomingValue(i));
            targets.insert(targets.end(), phi_targets.begin(), phi_targets.end());
        }
        
    } else if (auto* cast = dyn_cast<CastInst>(func_ptr)) {
        // 类型转换
        auto cast_targets = resolveWithHeuristics(cast->getOperand(0));
        targets.insert(targets.end(), cast_targets.begin(), cast_targets.end());
        
    } else {
        // 未知类型的函数指针
        targets.push_back("unknown_function_pointer");
    }
    
    // 去重
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    
    return targets;
}

//===----------------------------------------------------------------------===//
// SVF辅助函数
//===----------------------------------------------------------------------===//
