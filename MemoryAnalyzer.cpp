//===- MemoryAnalyzer.cpp - 基于调用图的内存操作分析器 --------------------===//

#include "SVFInterruptAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 单函数内存操作分析
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::analyzeMemoryOperationsInFunction(Function* function, InterruptHandlerResult& result) {
    std::string func_name = function->getName().str();
    
    for (auto& BB : *function) {
        for (auto& I : BB) {
            if (isWriteOperation(&I)) {
                result.memory_write_operations++;
                
                // 创建写操作记录
                MemoryWriteOperation write_op;
                write_op.write_count = 1;
                write_op.write_locations.push_back(getInstructionLocation(&I) + " (in " + func_name + ")");
                
                if (auto* SI = dyn_cast<StoreInst>(&I)) {
                    analyzeStoreInstruction(SI, write_op, func_name);
                } else if (auto* RMWI = dyn_cast<AtomicRMWInst>(&I)) {
                    analyzeAtomicRMWInstruction(RMWI, write_op, func_name);
                } else if (auto* CMPX = dyn_cast<AtomicCmpXchgInst>(&I)) {
                    analyzeAtomicCmpXchgInstruction(CMPX, write_op, func_name);
                }
                
                result.memory_writes.push_back(write_op);
                
            } else if (isReadOperation(&I)) {
                result.memory_read_operations++;
            }
        }
    }
}

void SVFInterruptAnalyzer::analyzeStoreInstruction(StoreInst* store, MemoryWriteOperation& write_op, const std::string& func_name) {
    Value* ptr = store->getPointerOperand();
    Value* value = store->getValueOperand();
    
    write_op.data_type = getTypeName(value->getType());
    
    if (auto* GV = dyn_cast<GlobalVariable>(ptr)) {
        // 直接写入全局变量
        write_op.target_name = GV->getName().str();
        write_op.target_type = getVariableScope(GV) == "global" ? "global_var" : "static_var";
        write_op.is_critical = true;
        
    } else if (auto* GEP = dyn_cast<GetElementPtrInst>(ptr)) {
        // 通过GEP写入结构体字段或数组元素
        analyzeGEPWriteOperation(GEP, write_op, func_name);
        
    } else if (auto* alloca = dyn_cast<AllocaInst>(ptr)) {
        // 写入局部变量
        write_op.target_name = alloca->hasName() ? alloca->getName().str() : "local_var";
        write_op.target_type = "local_var";
        write_op.is_critical = false;
        
    } else {
        // 其他情况（通过指针间接写入）
        write_op.target_name = ptr->hasName() ? ptr->getName().str() : "indirect_write";
        write_op.target_type = "indirect_write";
        write_op.is_critical = true;  // 间接写入可能很重要
    }
}

void SVFInterruptAnalyzer::analyzeAtomicRMWInstruction(AtomicRMWInst* rmw, MemoryWriteOperation& write_op, const std::string& func_name) {
    Value* ptr = rmw->getPointerOperand();
    
    write_op.data_type = getTypeName(rmw->getType());
    write_op.target_type = "atomic_rmw";
    write_op.is_critical = true;
    
    if (auto* GV = dyn_cast<GlobalVariable>(ptr)) {
        write_op.target_name = GV->getName().str() + "_atomic";
    } else {
        write_op.target_name = ptr->hasName() ? ptr->getName().str() + "_atomic" : "atomic_var";
    }
}

void SVFInterruptAnalyzer::analyzeAtomicCmpXchgInstruction(AtomicCmpXchgInst* cmpxchg, MemoryWriteOperation& write_op, const std::string& func_name) {
    Value* ptr = cmpxchg->getPointerOperand();
    
    write_op.data_type = getTypeName(cmpxchg->getNewValOperand()->getType());
    write_op.target_type = "atomic_cmpxchg";
    write_op.is_critical = true;
    
    if (auto* GV = dyn_cast<GlobalVariable>(ptr)) {
        write_op.target_name = GV->getName().str() + "_cmpxchg";
    } else {
        write_op.target_name = ptr->hasName() ? ptr->getName().str() + "_cmpxchg" : "cmpxchg_var";
    }
}

//===----------------------------------------------------------------------===//
// 全局和静态变量写操作分析
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::analyzeGlobalWritesInFunction(Function* function, 
                                                         std::set<std::string>& modified_globals,
                                                         std::set<std::string>& modified_statics) {
    std::string func_name = function->getName().str();
    
    for (auto& BB : *function) {
        for (auto& I : BB) {
            if (auto* SI = dyn_cast<StoreInst>(&I)) {
                analyzeStoreGlobalAccess(SI, modified_globals, modified_statics, func_name);
            } else if (auto* RMWI = dyn_cast<AtomicRMWInst>(&I)) {
                analyzeAtomicGlobalAccess(RMWI, modified_globals, modified_statics, func_name);
            } else if (auto* CMPX = dyn_cast<AtomicCmpXchgInst>(&I)) {
                analyzeAtomicCmpXchgGlobalAccess(CMPX, modified_globals, modified_statics, func_name);
            }
        }
    }
}

void SVFInterruptAnalyzer::analyzeStoreGlobalAccess(StoreInst* store,
                                                   std::set<std::string>& modified_globals,
                                                   std::set<std::string>& modified_statics,
                                                   const std::string& source_function) {
    Value* ptr = store->getPointerOperand();
    
    if (auto* GV = dyn_cast<GlobalVariable>(ptr)) {
        // 直接写入全局变量
        std::string var_name = GV->getName().str();
        std::string scope = getVariableScope(GV);
        std::string full_name = var_name + " (written in " + source_function + ")";
        
        if (scope == "global") {
            modified_globals.insert(full_name);
        } else if (scope == "static") {
            modified_statics.insert(full_name);
        }
        
    } else if (auto* GEP = dyn_cast<GetElementPtrInst>(ptr)) {
        // 通过GEP写入全局变量的字段
        analyzeGEPGlobalAccess(GEP, modified_globals, modified_statics, source_function);
        
    } else {
        // 检查是否是通过其他方式访问全局变量
        analyzeIndirectGlobalAccess(ptr, modified_globals, modified_statics, source_function);
    }
}

void SVFInterruptAnalyzer::analyzeGEPGlobalAccess(GetElementPtrInst* gep,
                                                 std::set<std::string>& modified_globals,
                                                 std::set<std::string>& modified_statics,
                                                 const std::string& source_function) {
    Value* base = gep->getPointerOperand();
    
    if (auto* GV = dyn_cast<GlobalVariable>(base)) {
        std::string var_name = GV->getName().str();
        std::string scope = getVariableScope(GV);
        std::string field_info = analyzeGEPFieldAccess(gep);
        std::string full_name = var_name + "." + field_info + " (written in " + source_function + ")";
        
        if (scope == "global") {
            modified_globals.insert(full_name);
        } else if (scope == "static") {
            modified_statics.insert(full_name);
        }
    } else {
        // 递归检查GEP的基址
        if (auto* base_gep = dyn_cast<GetElementPtrInst>(base)) {
            analyzeGEPGlobalAccess(base_gep, modified_globals, modified_statics, source_function);
        }
    }
}

void SVFInterruptAnalyzer::analyzeIndirectGlobalAccess(Value* ptr,
                                                      std::set<std::string>& modified_globals,
                                                      std::set<std::string>& modified_statics,
                                                      const std::string& source_function) {
    // 分析可能的间接全局变量访问
    if (auto* load = dyn_cast<LoadInst>(ptr)) {
        Value* loaded_from = load->getPointerOperand();
        if (auto* GV = dyn_cast<GlobalVariable>(loaded_from)) {
            // 这是通过加载全局指针进行的间接写入
            std::string var_name = GV->getName().str();
            std::string scope = getVariableScope(GV);
            std::string full_name = var_name + "_indirect (written in " + source_function + ")";
            
            if (scope == "global") {
                modified_globals.insert(full_name);
            } else if (scope == "static") {
                modified_statics.insert(full_name);
            }
        }
    }
}

void SVFInterruptAnalyzer::analyzeAtomicGlobalAccess(AtomicRMWInst* atomic,
                                                    std::set<std::string>& modified_globals,
                                                    std::set<std::string>& modified_statics,
                                                    const std::string& source_function) {
    Value* ptr = atomic->getPointerOperand();
    if (auto* GV = dyn_cast<GlobalVariable>(ptr)) {
        std::string var_name = GV->getName().str();
        std::string scope = getVariableScope(GV);
        std::string full_name = var_name + "_atomic (written in " + source_function + ")";
        
        if (scope == "global") {
            modified_globals.insert(full_name);
        } else if (scope == "static") {
            modified_statics.insert(full_name);
        }
    }
}

void SVFInterruptAnalyzer::analyzeAtomicCmpXchgGlobalAccess(AtomicCmpXchgInst* cmpxchg,
                                                           std::set<std::string>& modified_globals,
                                                           std::set<std::string>& modified_statics,
                                                           const std::string& source_function) {
    Value* ptr = cmpxchg->getPointerOperand();
    if (auto* GV = dyn_cast<GlobalVariable>(ptr)) {
        std::string var_name = GV->getName().str();
        std::string scope = getVariableScope(GV);
        std::string full_name = var_name + "_cmpxchg (written in " + source_function + ")";
        
        if (scope == "global") {
            modified_globals.insert(full_name);
        } else if (scope == "static") {
            modified_statics.insert(full_name);
        }
    }
}

//===----------------------------------------------------------------------===//
// 写操作合并和排序
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::consolidateWriteOperations(InterruptHandlerResult& result) {
    std::map<std::string, MemoryWriteOperation> consolidated;
    
    for (const auto& write_op : result.memory_writes) {
        std::string key = write_op.target_name + "_" + write_op.target_type;
        
        if (consolidated.find(key) != consolidated.end()) {
            // 合并现有的写操作
            consolidated[key].write_count += write_op.write_count;
            consolidated[key].write_locations.insert(
                consolidated[key].write_locations.end(),
                write_op.write_locations.begin(),
                write_op.write_locations.end()
            );
            consolidated[key].is_critical = consolidated[key].is_critical || write_op.is_critical;
        } else {
            consolidated[key] = write_op;
        }
    }
    
    // 重新构建写操作列表
    result.memory_writes.clear();
    for (const auto& pair : consolidated) {
        result.memory_writes.push_back(pair.second);
    }
    
    // 按重要性和频次排序
    std::sort(result.memory_writes.begin(), result.memory_writes.end(),
              [](const MemoryWriteOperation& a, const MemoryWriteOperation& b) {
                  if (a.is_critical != b.is_critical) {
                      return a.is_critical > b.is_critical;
                  }
                  return a.write_count > b.write_count;
              });
}

//===----------------------------------------------------------------------===//
// 工具函数
//===----------------------------------------------------------------------===//

bool SVFInterruptAnalyzer::isWriteOperation(const Instruction* inst) {
    return isa<StoreInst>(inst) || 
           isa<AtomicRMWInst>(inst) || 
           isa<AtomicCmpXchgInst>(inst);
}

bool SVFInterruptAnalyzer::isReadOperation(const Instruction* inst) {
    return isa<LoadInst>(inst);
}

bool SVFInterruptAnalyzer::isGlobalOrStaticVariable(Value* value) {
    return isa<GlobalVariable>(value);
}

std::string SVFInterruptAnalyzer::getVariableScope(GlobalVariable* gv) {
    if (gv->hasInternalLinkage() || gv->hasPrivateLinkage()) {
        return "static";
    } else {
        return "global";
    }
}

std::string SVFInterruptAnalyzer::analyzeGEPFieldAccess(GetElementPtrInst* gep) {
    std::string field_info = "field";
    
    // 分析GEP的索引来确定访问的字段
    if (gep->getNumIndices() >= 2) {
        auto indices = gep->indices();
        auto it = indices.begin();
        ++it; // 跳过第一个索引（通常是0）
        
        if (it != indices.end()) {
            if (auto* CI = dyn_cast<ConstantInt>(*it)) {
                unsigned field_index = CI->getZExtValue();
                field_info = "field_" + std::to_string(field_index);
                
                // 尝试获取结构体类型信息
                Type* source_type = gep->getSourceElementType();
                if (auto* struct_type = dyn_cast<StructType>(source_type)) {
                    std::string struct_name = getStructName(struct_type);
                    if (!struct_name.empty()) {
                        field_info = struct_name + "::field_" + std::to_string(field_index);
                    }
                }
            } else {
                field_info = "dynamic_field";
            }
        }
    }
    
    return field_info;
}
