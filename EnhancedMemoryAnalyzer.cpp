//===- EnhancedMemoryAnalyzer.cpp - 增强内存访问分析器实现 ----------------===//

#include "CrossModuleAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// EnhancedCrossModuleMemoryAnalyzer 实现
//===----------------------------------------------------------------------===//

std::vector<MemoryAccessInfo> EnhancedCrossModuleMemoryAnalyzer::analyzeWithDataFlow(Function& F) {
    std::vector<MemoryAccessInfo> accesses;
    
    for (auto& BB : F) {
        for (auto& I : BB) {
            MemoryAccessInfo info;
            
            if (auto* LI = dyn_cast<LoadInst>(&I)) {
                info = analyzePointerDataFlow(LI->getPointerOperand(), false, LI->getType());
                
            } else if (auto* SI = dyn_cast<StoreInst>(&I)) {
                info = analyzePointerDataFlow(SI->getPointerOperand(), true, 
                                             SI->getValueOperand()->getType());
                
            } else if (auto* RMWI = dyn_cast<AtomicRMWInst>(&I)) {
                info = analyzePointerDataFlow(RMWI->getPointerOperand(), true, RMWI->getType());
                info.is_atomic = true;
                
            } else if (auto* CXI = dyn_cast<AtomicCmpXchgInst>(&I)) {
                info = analyzePointerDataFlow(CXI->getPointerOperand(), true, 
                                            CXI->getCompareOperand()->getType());
                info.is_atomic = true;
            }
            
            // 添加源码位置信息
            const DebugLoc &DI = I.getDebugLoc();
            if (DI) {
                info.source_location = DI->getFilename().str() + ":" + 
                                     std::to_string(DI->getLine());
            }
            
            if (info.confidence > 0) {
                accesses.push_back(info);
            }
        }
    }
    
    return accesses;
}

MemoryAccessInfo EnhancedCrossModuleMemoryAnalyzer::analyzePointerDataFlow(Value* ptr, bool is_write, Type* accessed_type) {
    MemoryAccessInfo info;
    info.is_write = is_write;
    
    if (!ptr) {
        return info;
    }
    
    // 设置访问大小 - 使用 getDataLayout() 方法
    const DataLayout* dl = getDataLayout();
    if (dl && accessed_type) {
        info.access_size = dl->getTypeStoreSize(accessed_type);
    }
    
    // 使用数据流分析器分析指针来源
    DataFlowNode flow_info = dataflow_analyzer->getDataFlowInfo(ptr);
    info.confidence = flow_info.confidence;
    
    if (flow_info.node_type == "global") {
        info.type = MemoryAccessInfo::GLOBAL_VARIABLE;
        info.symbol_name = flow_info.source_info;
        info.chain_description = "global_variable_dataflow_confirmed";
        
        // 尝试在全局符号表中确认这个变量
        size_t colon_pos = flow_info.source_info.find(':');
        if (colon_pos != std::string::npos) {
            std::string var_name = flow_info.source_info.substr(colon_pos + 1);
            if (cross_analyzer->findGlobalVariable(var_name) != nullptr) {
                info.confidence = std::min(info.confidence + 10, 100);
                info.chain_description += "_cross_module_confirmed";
            }
        }
        
    } else if (flow_info.node_type == "static") {
        info.type = MemoryAccessInfo::GLOBAL_VARIABLE; // 静态变量也归类为全局访问
        info.symbol_name = flow_info.source_info;
        info.chain_description = "static_variable_dataflow_confirmed";
        
        // 尝试在静态符号表中确认这个变量
        size_t colon_pos = flow_info.source_info.find(':');
        if (colon_pos != std::string::npos) {
            std::string var_name = flow_info.source_info.substr(colon_pos + 1);
            std::string module_hint = "";
            if (flow_info.source_module) {
                module_hint = flow_info.source_module->getName().str();
            }
            if (cross_analyzer->findGlobalVariable(var_name, module_hint) != nullptr) {
                info.confidence = std::min(info.confidence + 8, 100);
                info.chain_description += "_static_cross_module_confirmed";
            }
        }
        
    } else if (flow_info.node_type == "parameter") {
        // 判断是否是IRQ处理函数的参数
        if (flow_info.source_info.find("parameter:0") != std::string::npos) {
            info.type = MemoryAccessInfo::IRQ_HANDLER_IRQ_ACCESS;
            info.symbol_name = "irq_parameter";
        } else if (flow_info.source_info.find("parameter:1") != std::string::npos) {
            info.type = MemoryAccessInfo::IRQ_HANDLER_DEV_ID_ACCESS;
            info.symbol_name = "dev_id_parameter";
        } else {
            info.type = MemoryAccessInfo::INDIRECT_ACCESS;
            info.symbol_name = flow_info.source_info;
        }
        info.chain_description = "function_parameter_dataflow";
        
    } else {
        info.type = MemoryAccessInfo::INDIRECT_ACCESS;
        info.symbol_name = flow_info.source_info;
        info.chain_description = "local_computation_dataflow";
    }
    
    return info;
}

MemoryAccessInfo EnhancedCrossModuleMemoryAnalyzer::analyzeGlobalVariableAccess(GlobalVariable* GV) {
    MemoryAccessInfo info;
    
    if (!GV) {
        return info;
    }
    
    info.type = MemoryAccessInfo::GLOBAL_VARIABLE;
    info.symbol_name = GV->getName().str();
    info.confidence = 90;
    
    // 确定是全局还是静态
    SymbolScope scope = cross_analyzer->getGlobalVariableScope(GV);
    if (scope == SymbolScope::GLOBAL) {
        info.chain_description = "confirmed_global_variable";
        info.confidence = 95;
    } else if (scope == SymbolScope::STATIC) {
        info.chain_description = "confirmed_static_variable";
        info.confidence = 90;
    } else {
        info.chain_description = "other_scope_variable";
        info.confidence = 85;
    }
    
    // 使用 getDataLayout() 方法
    const DataLayout* dl = getDataLayout();
    if (dl) {
        info.access_size = dl->getTypeStoreSize(GV->getValueType());
    }
    
    return info;
}
