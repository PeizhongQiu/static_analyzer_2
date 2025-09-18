//===- DataFlowAnalyzer.cpp - 数据流分析器实现 ---------------------------===//

#include "CrossModuleAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// DataFlowAnalyzer 实现
//===----------------------------------------------------------------------===//

DataFlowNode DataFlowAnalyzer::analyzeValueSource(Value* V, int depth) {
    if (depth > 10) { // 防止无限递归
        DataFlowNode node;
        node.confidence = 10;
        node.node_type = "recursive_limit";
        return node;
    }
    
    // 检查缓存
    if (value_to_node_cache.find(V) != value_to_node_cache.end()) {
        return value_to_node_cache[V];
    }
    
    DataFlowNode node;
    node.value = V;
    
    if (auto* GV = dyn_cast<GlobalVariable>(V)) {
        // 全局变量
        auto it = global_symbols->global_variables.find(GV->getName().str());
        if (it != global_symbols->global_variables.end()) {
            node.node_type = "global";
            node.source_info = "global_variable:" + GV->getName().str();
            node.confidence = 95;
            node.source_module = global_symbols->global_var_to_module[GV];
        } else {
            // 可能是静态变量
            node.node_type = "static";
            node.source_info = "static_variable:" + GV->getName().str();
            node.confidence = 90;
            node.source_module = global_symbols->global_var_to_module[GV];
        }
    } else if (auto* Arg = dyn_cast<Argument>(V)) {
        // 函数参数
        node.node_type = "parameter";
        node.source_info = "function_parameter:" + std::to_string(Arg->getArgNo());
        node.confidence = 85;
        Function* F = Arg->getParent();
        if (F) {
            node.source_module = global_symbols->function_to_module[F];
        }
    } else if (auto* LI = dyn_cast<LoadInst>(V)) {
        // Load指令 - 分析被加载的内存位置
        DataFlowNode load_node = analyzeLoadDataFlow(LI);
        node = load_node;
        node.confidence = std::max(load_node.confidence - 10, 30);
    } else if (auto* GEP = dyn_cast<GetElementPtrInst>(V)) {
        // GEP指令 - 分析基指针
        DataFlowNode gep_node = analyzeGEPDataFlow(GEP);
        node = gep_node;
        node.confidence = std::max(gep_node.confidence - 5, 40);
    } else if (auto* PHI = dyn_cast<PHINode>(V)) {
        // PHI节点 - 合并多个数据流
        DataFlowNode phi_node = analyzePHIDataFlow(PHI);
        node = phi_node;
        node.confidence = std::max(phi_node.confidence - 15, 25);
    } else if (isa<Constant>(V)) {
        // 常量
        node.node_type = "constant";
        node.source_info = "constant_value";
        node.confidence = 100;
    } else {
        // 其他情况
        node.node_type = "local";
        node.source_info = "local_computation";
        node.confidence = 50;
    }
    
    // 缓存结果
    value_to_node_cache[V] = node;
    return node;
}

DataFlowNode DataFlowAnalyzer::analyzeLoadDataFlow(LoadInst* LI) {
    Value* ptr = LI->getPointerOperand();
    return analyzeValueSource(ptr, 1); // 递归分析指针来源
}

DataFlowNode DataFlowAnalyzer::analyzeGEPDataFlow(GetElementPtrInst* GEP) {
    Value* base = GEP->getPointerOperand();
    DataFlowNode base_node = analyzeValueSource(base, 1);
    
    // GEP保持基指针的性质，但可能降低置信度
    base_node.source_info += "_gep_access";
    return base_node;
}

DataFlowNode DataFlowAnalyzer::analyzePHIDataFlow(PHINode* PHI) {
    DataFlowNode result;
    result.value = PHI;
    result.confidence = 0;
    
    std::map<std::string, int> type_votes;
    int total_confidence = 0;
    
    // 分析所有输入值
    for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
        Value* incoming = PHI->getIncomingValue(i);
        DataFlowNode incoming_node = analyzeValueSource(incoming, 1);
        
        type_votes[incoming_node.node_type] += incoming_node.confidence;
        total_confidence += incoming_node.confidence;
        
        if (result.source_module == nullptr) {
            result.source_module = incoming_node.source_module;
        }
    }
    
    // 选择置信度最高的类型
    std::string best_type = "local";
    int best_confidence = 0;
    for (const auto& vote : type_votes) {
        if (vote.second > best_confidence) {
            best_confidence = vote.second;
            best_type = vote.first;
        }
    }
    
    result.node_type = best_type;
    result.source_info = "phi_merge:" + best_type;
    result.confidence = total_confidence > 0 ? best_confidence * 100 / total_confidence : 30;
    
    return result;
}

bool DataFlowAnalyzer::isGlobalVariable(Value* V) {
    DataFlowNode node = getDataFlowInfo(V);
    return node.node_type == "global";
}

bool DataFlowAnalyzer::isStaticVariable(Value* V) {
    DataFlowNode node = getDataFlowInfo(V);
    return node.node_type == "static";
}

DataFlowNode DataFlowAnalyzer::getDataFlowInfo(Value* V) {
    return analyzeValueSource(V, 0);
}
