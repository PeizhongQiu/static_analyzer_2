//===- MemoryAccessAnalyzer.cpp - Memory Access Analyzer Implementation ===//

#include "MemoryAccessAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

std::string PointerChain::toString() const {
    std::string result;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) result += "->";
        
        const auto& elem = elements[i];
        switch (elem.type) {
            case PointerChainElement::GLOBAL_VAR_BASE:
                result += elem.symbol_name;
                break;
            case PointerChainElement::IRQ_HANDLER_ARG0:
                result += "irq";
                break;
            case PointerChainElement::IRQ_HANDLER_ARG1:
                result += "dev_id";
                break;
            case PointerChainElement::STRUCT_FIELD_DEREF:
                if (!elem.struct_type_name.empty()) {
                    result += elem.struct_type_name + "[" + std::to_string(elem.offset) + "]";
                } else {
                    result += "field_" + std::to_string(elem.offset);
                }
                break;
            case PointerChainElement::ARRAY_INDEX_DEREF:
                result += "array[" + std::to_string(elem.offset) + "]";
                break;
            case PointerChainElement::DIRECT_LOAD:
                result += "*(" + elem.symbol_name + ")";
                break;
            case PointerChainElement::CONSTANT_OFFSET:
                result += "0x" + std::to_string(elem.offset);
                break;
        }
    }
    return result;
}

std::vector<MemoryAccessInfo> MemoryAccessAnalyzer::analyzeFunction(Function &F) {
    std::vector<MemoryAccessInfo> accesses;
    
    // 清空缓存（每个函数重新开始）
    pointer_chain_cache.clear();
    
    // 检查函数是否是中断处理函数签名
    bool is_irq_handler = isIRQHandlerFunction(F);
    
    for (auto &BB : F) {
        for (auto &I : BB) {
            MemoryAccessInfo info;
            
            if (auto *LI = dyn_cast<LoadInst>(&I)) {
                info = analyzeLoadStoreWithChain(LI->getPointerOperand(), false, 
                                                LI->getType(), is_irq_handler);
                
            } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
                info = analyzeLoadStoreWithChain(SI->getPointerOperand(), true, 
                                               SI->getValueOperand()->getType(), is_irq_handler);
                
            } else if (auto *RMWI = dyn_cast<AtomicRMWInst>(&I)) {
                info = analyzeLoadStoreWithChain(RMWI->getPointerOperand(), true, 
                                                RMWI->getType(), is_irq_handler);
                info.is_atomic = true;
                
            } else if (auto *CXI = dyn_cast<AtomicCmpXchgInst>(&I)) {
                info = analyzeLoadStoreWithChain(CXI->getPointerOperand(), true, 
                                               CXI->getCompareOperand()->getType(), is_irq_handler);
                info.is_atomic = true;
            }
            
            // 添加源码位置信息
            if (auto *DI = I.getDebugLoc()) {
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

bool MemoryAccessAnalyzer::isIRQHandlerFunction(Function &F) {
    // 检查中断处理函数签名：irqreturn_t handler(int irq, void *dev_id)
    if (F.getReturnType()->isIntegerTy() && F.arg_size() == 2) {
        auto arg_it = F.arg_begin();
        Type *first_arg_type = arg_it->getType();
        Type *second_arg_type = (++arg_it)->getType();
        
        return first_arg_type->isIntegerTy(32) && second_arg_type->isPointerTy();
    }
    return false;
}

PointerChain MemoryAccessAnalyzer::tracePointerChain(Value *ptr, int depth) {
    PointerChain chain;
    
    // 防止递归过深
    if (depth > MAX_CHAIN_DEPTH) {
        chain.confidence = 10;
        return chain;
    }
    
    // 检查缓存
    if (pointer_chain_cache.find(ptr) != pointer_chain_cache.end()) {
        return pointer_chain_cache[ptr];
    }
    
    PointerChainElement element;
    
    if (auto *GV = dyn_cast<GlobalVariable>(ptr)) {
        // 全局变量 - 链的起点
        element.type = PointerChainElement::GLOBAL_VAR_BASE;
        element.symbol_name = GV->getName().str();
        element.llvm_value = ptr;
        chain.elements.push_back(element);
        chain.confidence = 95;
        chain.is_complete = true;
        
    } else if (auto *Arg = dyn_cast<Argument>(ptr)) {
        // 函数参数 - 需要特别处理中断处理函数的参数
        Function *F = Arg->getParent();
        if (isIRQHandlerFunction(*F)) {
            if (Arg->getArgNo() == 0) {
                element.type = PointerChainElement::IRQ_HANDLER_ARG0;
                element.symbol_name = "irq";
            } else if (Arg->getArgNo() == 1) {
                element.type = PointerChainElement::IRQ_HANDLER_ARG1;
                element.symbol_name = "dev_id";
            }
            element.llvm_value = ptr;
            chain.elements.push_back(element);
            chain.confidence = 90;
            chain.is_complete = true;
        } else {
            // 非中断处理函数的参数，置信度较低
            element.type = PointerChainElement::DIRECT_LOAD;
            element.symbol_name = "func_arg_" + std::to_string(Arg->getArgNo());
            element.llvm_value = ptr;
            chain.elements.push_back(element);
            chain.confidence = 40;
            chain.is_complete = false;
        }
        
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(ptr)) {
        // GEP指令 - 需要递归追踪基指针
        PointerChain base_chain = tracePointerChain(GEP->getPointerOperand(), depth + 1);
        
        // 分析GEP操作
        element.type = PointerChainElement::STRUCT_FIELD_DEREF;
        element.llvm_value = ptr;
        
        Type *source_type = GEP->getSourceElementType();
        if (auto *struct_type = dyn_cast<StructType>(source_type)) {
            element.struct_type_name = struct_type->getName().str();
            
            // 获取字段索引
            if (GEP->getNumOperands() >= 3) {
                if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                    element.offset = CI->getSExtValue();
                }
            }
        } else if (source_type->isArrayTy()) {
            element.type = PointerChainElement::ARRAY_INDEX_DEREF;
            if (GEP->getNumOperands() >= 3) {
                if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                    element.offset = CI->getSExtValue();
                }
            }
        }
        
        // 合并基链和当前元素
        chain.elements = base_chain.elements;
        chain.elements.push_back(element);
        chain.confidence = std::max(base_chain.confidence - 5, 40);
        chain.is_complete = base_chain.is_complete;
        
    } else if (auto *LI = dyn_cast<LoadInst>(ptr)) {
        // Load指令 - 间接访问，需要追踪被load的指针
        PointerChain loaded_chain = tracePointerChain(LI->getPointerOperand(), depth + 1);
        
        element.type = PointerChainElement::DIRECT_LOAD;
        element.llvm_value = ptr;
        
        // 这是一个解引用操作
        chain.elements = loaded_chain.elements;
        chain.elements.push_back(element);
        chain.confidence = std::max(loaded_chain.confidence - 10, 30);
        chain.is_complete = loaded_chain.is_complete;
        
    } else if (auto *CI = dyn_cast<ConstantInt>(ptr)) {
        // 常量指针
        element.type = PointerChainElement::CONSTANT_OFFSET;
        element.offset = CI->getSExtValue();
        element.llvm_value = ptr;
        chain.elements.push_back(element);
        chain.confidence = 100;
        chain.is_complete = true;
        
    } else if (auto *CE = dyn_cast<ConstantExpr>(ptr)) {
        // 常量表达式，可能是全局变量的地址计算
        if (CE->getOpcode() == Instruction::GetElementPtr) {
            // 分析常量GEP
            if (auto *GV = dyn_cast<GlobalVariable>(CE->getOperand(0))) {
                element.type = PointerChainElement::GLOBAL_VAR_BASE;
                element.symbol_name = GV->getName().str();
                element.llvm_value = ptr;
                chain.elements.push_back(element);
                
                // 如果有偏移，添加偏移元素
                if (CE->getNumOperands() > 2) {
                    if (auto *offset_CI = dyn_cast<ConstantInt>(CE->getOperand(2))) {
                        PointerChainElement offset_elem;
                        offset_elem.type = PointerChainElement::STRUCT_FIELD_DEREF;
                        offset_elem.offset = offset_CI->getSExtValue();
                        offset_elem.llvm_value = ptr;
                        chain.elements.push_back(offset_elem);
                    }
                }
                
                chain.confidence = 90;
                chain.is_complete = true;
            }
        }
        
    } else if (auto *PHI = dyn_cast<PHINode>(ptr)) {
        // PHI节点 - 合并多个可能的指针来源
        std::vector<PointerChain> incoming_chains;
        int total_confidence = 0;
        
        for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
            PointerChain incoming_chain = tracePointerChain(PHI->getIncomingValue(i), depth + 1);
            incoming_chains.push_back(incoming_chain);
            total_confidence += incoming_chain.confidence;
        }
        
        if (!incoming_chains.empty()) {
            // 使用第一个链作为基础，但降低置信度
            chain = incoming_chains[0];
            chain.confidence = total_confidence / incoming_chains.size() * 0.8; // 降低20%
            chain.is_complete = false; // PHI节点使得分析不完整
        }
        
    } else {
        // 其他情况 - 无法追踪
        element.type = PointerChainElement::DIRECT_LOAD;
        element.symbol_name = "unknown";
        element.llvm_value = ptr;
        chain.elements.push_back(element);
        chain.confidence = 20;
        chain.is_complete = false;
    }
    
    // 缓存结果
    pointer_chain_cache[ptr] = chain;
    return chain;
}

MemoryAccessInfo MemoryAccessAnalyzer::analyzeLoadStoreWithChain(Value *ptr, bool is_write, 
                                                                Type *accessed_type, bool is_irq_handler) {
    MemoryAccessInfo info;
    info.is_write = is_write;
    
    // 设置访问大小
    if (DL && accessed_type) {
        info.access_size = DL->getTypeStoreSize(accessed_type);
    }
    
    // 追踪完整的指针链
    PointerChain chain = tracePointerChain(ptr);
    info.pointer_chain = chain;
    info.chain_description = chain.toString();
    info.confidence = chain.confidence;
    
    // 根据链的分析结果设置访问类型
    if (chain.elements.empty()) {
        info.type = MemoryAccessInfo::INDIRECT_ACCESS;
        info.confidence = 20;
    } else {
        const auto& first_elem = chain.elements[0];
        const auto& last_elem = chain.elements.back();
        
        if (first_elem.type == PointerChainElement::GLOBAL_VAR_BASE && chain.elements.size() == 1) {
            // 直接全局变量访问
            info.type = MemoryAccessInfo::GLOBAL_VARIABLE;
            info.symbol_name = first_elem.symbol_name;
            
        } else if (first_elem.type == PointerChainElement::IRQ_HANDLER_ARG0) {
            // 通过irq参数访问（通常很少见）
            info.type = MemoryAccessInfo::IRQ_HANDLER_IRQ_ACCESS;
            info.symbol_name = "irq_param";
            
        } else if (first_elem.type == PointerChainElement::IRQ_HANDLER_ARG1) {
            // 通过dev_id参数访问（最常见的中断处理函数访问模式）
            if (chain.elements.size() == 1) {
                // 直接访问dev_id参数本身
                info.type = MemoryAccessInfo::IRQ_HANDLER_DEV_ID_ACCESS;
                info.symbol_name = "dev_id_param";
            } else {
                // 通过dev_id的指针链访问
                info.type = MemoryAccessInfo::POINTER_CHAIN_ACCESS;
                
                // 构建更详细的符号名，便于fuzzing使用
                std::string detailed_name = "dev_id";
                for (size_t i = 1; i < chain.elements.size(); ++i) {
                    const auto& elem = chain.elements[i];
                    if (elem.type == PointerChainElement::STRUCT_FIELD_DEREF) {
                        if (!elem.struct_type_name.empty()) {
                            detailed_name += "->" + elem.struct_type_name + 
                                           "_offset_" + std::to_string(elem.offset);
                        } else {
                            detailed_name += "->field_" + std::to_string(elem.offset);
                        }
                    } else if (elem.type == PointerChainElement::ARRAY_INDEX_DEREF) {
                        detailed_name += "->array[" + std::to_string(elem.offset) + "]";
                    } else if (elem.type == PointerChainElement::DIRECT_LOAD) {
                        detailed_name += "->*ptr";
                    }
                }
                info.symbol_name = detailed_name;
                
                // 设置最终访问的结构体信息
                if (last_elem.type == PointerChainElement::STRUCT_FIELD_DEREF) {
                    info.struct_type_name = last_elem.struct_type_name;
                    info.offset = last_elem.offset;
                }
            }
            
        } else if (chain.elements.size() > 1) {
            // 其他多级指针链访问
            info.type = MemoryAccessInfo::POINTER_CHAIN_ACCESS;
            
            // 设置最终访问的结构体信息
            if (last_elem.type == PointerChainElement::STRUCT_FIELD_DEREF) {
                info.struct_type_name = last_elem.struct_type_name;
                info.offset = last_elem.offset;
            } else if (last_elem.type == PointerChainElement::ARRAY_INDEX_DEREF) {
                info.type = MemoryAccessInfo::ARRAY_ELEMENT;
                info.offset = last_elem.offset;
            }
            
            // 构建符号名
            info.symbol_name = chain.toString();
            
        } else {
            // 单个元素的其他类型
            if (first_elem.type == PointerChainElement::CONSTANT_OFFSET) {
                info.type = MemoryAccessInfo::CONSTANT_ADDRESS;
                info.offset = first_elem.offset;
                info.confidence = 100;
            } else {
                info.type = MemoryAccessInfo::INDIRECT_ACCESS;
                info.symbol_name = first_elem.symbol_name;
            }
        }
    }
    
    return info;
}

MemoryAccessInfo MemoryAccessAnalyzer::analyzeGEPInstruction(GetElementPtrInst *GEP) {
    MemoryAccessInfo info;
    
    Type *source_type = GEP->getSourceElementType();
    if (auto *struct_type = dyn_cast<StructType>(source_type)) {
        info.type = MemoryAccessInfo::STRUCT_FIELD_ACCESS;
        info.struct_type_name = struct_type->getName().str();
        
        // 计算字段偏移
        if (GEP->getNumOperands() >= 3) {
            if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                info.offset = CI->getSExtValue();
                info.confidence = 90;
                
                // 获取字段类型信息
                if (info.offset < struct_type->getNumElements()) {
                    Type *field_type = struct_type->getElementType(info.offset);
                    if (DL) {
                        info.access_size = DL->getTypeStoreSize(field_type);
                    }
                }
            }
        }
    } else if (source_type->isArrayTy()) {
        info.type = MemoryAccessInfo::ARRAY_ELEMENT;
        info.confidence = 80;
        
        // 获取数组索引
        if (GEP->getNumOperands() >= 3) {
            if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                info.offset = CI->getSExtValue();
            }
        }
    }
    
    return info;
}

MemoryAccessInfo MemoryAccessAnalyzer::analyzeGlobalVariable(GlobalVariable *GV) {
    MemoryAccessInfo info;
    info.type = MemoryAccessInfo::GLOBAL_VARIABLE;
    info.symbol_name = GV->getName().str();
    info.confidence = 95;
    
    if (DL) {
        info.access_size = DL->getTypeStoreSize(GV->getValueType());
    }
    
    // 创建简单的指针链
    PointerChainElement elem;
    elem.type = PointerChainElement::GLOBAL_VAR_BASE;
    elem.symbol_name = GV->getName().str();
    elem.llvm_value = GV;
    
    info.pointer_chain.elements.push_back(elem);
    info.pointer_chain.confidence = 95;
    info.pointer_chain.is_complete = true;
    info.chain_description = GV->getName().str();
    
    return info;
}

MemoryAccessInfo MemoryAccessAnalyzer::analyzeLoadStore(Value *ptr, bool is_write) {
    // 保留旧的简单分析方法作为后备
    MemoryAccessInfo info;
    info.is_write = is_write;
    
    if (auto *GV = dyn_cast<GlobalVariable>(ptr)) {
        return analyzeGlobalVariable(GV);
        
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(ptr)) {
        return analyzeGEPInstruction(GEP);
        
    } else if (auto *CI = dyn_cast<ConstantInt>(ptr)) {
        info.type = MemoryAccessInfo::CONSTANT_ADDRESS;
        info.offset = CI->getSExtValue();
        info.confidence = 100;
        
    } else if (isa<Argument>(ptr)) {
        info.type = MemoryAccessInfo::IRQ_HANDLER_DEV_ID_ACCESS; // 假设是dev_id
        info.confidence = 60;
    } else {
        info.type = MemoryAccessInfo::INDIRECT_ACCESS;
        info.confidence = 30;
    }
    
    return info;
}
