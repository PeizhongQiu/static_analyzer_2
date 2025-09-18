//===- MemoryAccessAnalyzer.cpp - Safe Memory Access Analyzer -----------===//

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

// 安全的编译器符号检查
bool MemoryAccessAnalyzer::isCompilerGeneratedSymbol(const std::string& symbol_name) const {
    // 安全检查：空字符串直接返回false
    if (symbol_name.empty()) {
        return false;
    }
    
    // 只过滤最明显的LLVM gcov符号，避免过度过滤
    if (symbol_name.find("__llvm_gcov_ctr") != std::string::npos) {
        return true;
    }
    
    // 可以根据需要添加更多过滤规则，但要谨慎
    return false;
}

// 安全的指针链检查
bool MemoryAccessAnalyzer::containsCompilerGeneratedSymbol(const PointerChain& chain) const {
    for (const auto& elem : chain.elements) {
        // 安全检查：确保symbol_name不为空再检查
        if (!elem.symbol_name.empty() && isCompilerGeneratedSymbol(elem.symbol_name)) {
            return true;
        }
    }
    return false;
}

std::vector<MemoryAccessInfo> MemoryAccessAnalyzer::analyzeFunction(Function &F) {
    std::vector<MemoryAccessInfo> accesses;
    
    // Clear cache (restart for each function)
    pointer_chain_cache.clear();
    
    // Check if function is IRQ handler signature
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
            
            // Add source location information - Fixed DebugLoc handling
            const DebugLoc &DI = I.getDebugLoc();
            if (DI) {
                info.source_location = DI->getFilename().str() + ":" + 
                                     std::to_string(DI->getLine());
            }
            
            // 只有置信度大于0才考虑
            if (info.confidence > 0) {
                // 应用过滤逻辑，但更加保守
                bool should_filter = false;
                
                // 安全检查：只在字符串非空时检查
                if (!info.symbol_name.empty() && isCompilerGeneratedSymbol(info.symbol_name)) {
                    should_filter = true;
                }
                
                // 检查指针链中的符号
                if (!should_filter && containsCompilerGeneratedSymbol(info.pointer_chain)) {
                    should_filter = true;
                }
                
                // 检查链描述
                if (!should_filter && !info.chain_description.empty() && 
                    isCompilerGeneratedSymbol(info.chain_description)) {
                    should_filter = true;
                }
                
                // 如果不需要过滤，添加到结果中
                if (!should_filter) {
                    accesses.push_back(info);
                }
            }
        }
    }
    
    return accesses;
}

bool MemoryAccessAnalyzer::isIRQHandlerFunction(Function &F) {
    // Check IRQ handler signature: irqreturn_t handler(int irq, void *dev_id)
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
    
    // Prevent excessive recursion
    if (depth > MAX_CHAIN_DEPTH) {
        chain.confidence = 10;
        return chain;
    }
    
    // 安全检查：确保ptr不为空
    if (!ptr) {
        chain.confidence = 0;
        return chain;
    }
    
    // Check cache
    if (pointer_chain_cache.find(ptr) != pointer_chain_cache.end()) {
        return pointer_chain_cache[ptr];
    }
    
    PointerChainElement element;
    
    if (auto *GV = dyn_cast<GlobalVariable>(ptr)) {
        // Global variable - chain starting point
        element.type = PointerChainElement::GLOBAL_VAR_BASE;
        element.symbol_name = GV->getName().str();
        element.llvm_value = ptr;
        chain.elements.push_back(element);
        chain.confidence = 95;
        chain.is_complete = true;
        
    } else if (auto *Arg = dyn_cast<Argument>(ptr)) {
        // Function argument - special handling for IRQ handler arguments
        Function *F = Arg->getParent();
        if (F && isIRQHandlerFunction(*F)) {
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
            // Non-IRQ handler function argument, lower confidence
            element.type = PointerChainElement::DIRECT_LOAD;
            element.symbol_name = "func_arg_" + std::to_string(Arg->getArgNo());
            element.llvm_value = ptr;
            chain.elements.push_back(element);
            chain.confidence = 40;
            chain.is_complete = false;
        }
        
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(ptr)) {
        // GEP instruction - need to recursively trace base pointer
        Value *basePtr = GEP->getPointerOperand();
        if (basePtr) {
            PointerChain base_chain = tracePointerChain(basePtr, depth + 1);
            
            // Analyze GEP operation
            element.type = PointerChainElement::STRUCT_FIELD_DEREF;
            element.llvm_value = ptr;
            
            Type *source_type = GEP->getSourceElementType();
            if (auto *struct_type = dyn_cast<StructType>(source_type)) {
                element.struct_type_name = struct_type->getName().str();
                
                // Get field index
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
            
            // Merge base chain and current element
            chain.elements = base_chain.elements;
            chain.elements.push_back(element);
            chain.confidence = std::max(base_chain.confidence - 5, 40);
            chain.is_complete = base_chain.is_complete;
        }
        
    } else if (auto *LI = dyn_cast<LoadInst>(ptr)) {
        // Load instruction - indirect access, trace the loaded pointer
        Value *loadPtr = LI->getPointerOperand();
        if (loadPtr) {
            PointerChain loaded_chain = tracePointerChain(loadPtr, depth + 1);
            
            element.type = PointerChainElement::DIRECT_LOAD;
            element.llvm_value = ptr;
            
            // This is a dereference operation
            chain.elements = loaded_chain.elements;
            chain.elements.push_back(element);
            chain.confidence = std::max(loaded_chain.confidence - 10, 30);
            chain.is_complete = loaded_chain.is_complete;
        }
        
    } else if (auto *CI = dyn_cast<ConstantInt>(ptr)) {
        // Constant pointer
        element.type = PointerChainElement::CONSTANT_OFFSET;
        element.offset = CI->getSExtValue();
        element.llvm_value = ptr;
        chain.elements.push_back(element);
        chain.confidence = 100;
        chain.is_complete = true;
        
    } else if (auto *CE = dyn_cast<ConstantExpr>(ptr)) {
        // Constant expression, possibly global variable address calculation
        if (CE->getOpcode() == Instruction::GetElementPtr) {
            // Analyze constant GEP
            if (CE->getNumOperands() > 0) {
                if (auto *GV = dyn_cast<GlobalVariable>(CE->getOperand(0))) {
                    element.type = PointerChainElement::GLOBAL_VAR_BASE;
                    element.symbol_name = GV->getName().str();
                    element.llvm_value = ptr;
                    chain.elements.push_back(element);
                    
                    // If there's an offset, add offset element
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
        }
        
    } else if (auto *PHI = dyn_cast<PHINode>(ptr)) {
        // PHI node - merge multiple possible pointer sources
        std::vector<PointerChain> incoming_chains;
        int total_confidence = 0;
        
        for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
            Value *incomingValue = PHI->getIncomingValue(i);
            if (incomingValue) {
                PointerChain incoming_chain = tracePointerChain(incomingValue, depth + 1);
                incoming_chains.push_back(incoming_chain);
                total_confidence += incoming_chain.confidence;
            }
        }
        
        if (!incoming_chains.empty()) {
            // Use first chain as base, but reduce confidence
            chain = incoming_chains[0];
            chain.confidence = total_confidence / incoming_chains.size() * 0.8; // Reduce by 20%
            chain.is_complete = false; // PHI node makes analysis incomplete
        }
        
    } else {
        // Other cases - cannot trace
        element.type = PointerChainElement::DIRECT_LOAD;
        element.symbol_name = "unknown";
        element.llvm_value = ptr;
        chain.elements.push_back(element);
        chain.confidence = 20;
        chain.is_complete = false;
    }
    
    // Cache result
    pointer_chain_cache[ptr] = chain;
    return chain;
}

// 其余方法保持不变，从原文件复制
MemoryAccessInfo MemoryAccessAnalyzer::analyzeLoadStoreWithChain(Value *ptr, bool is_write, 
                                                                Type *accessed_type, bool is_irq_handler) {
    MemoryAccessInfo info;
    info.is_write = is_write;
    
    // 安全检查
    if (!ptr) {
        info.confidence = 0;
        return info;
    }
    
    // Set access size
    if (DL && accessed_type) {
        info.access_size = DL->getTypeStoreSize(accessed_type);
    }
    
    // Trace complete pointer chain
    PointerChain chain = tracePointerChain(ptr);
    info.pointer_chain = chain;
    info.chain_description = chain.toString();
    info.confidence = chain.confidence;
    
    // Set access type based on chain analysis
    if (chain.elements.empty()) {
        info.type = MemoryAccessInfo::INDIRECT_ACCESS;
        info.confidence = 20;
    } else {
        const auto& first_elem = chain.elements[0];
        const auto& last_elem = chain.elements.back();
        
        if (first_elem.type == PointerChainElement::GLOBAL_VAR_BASE && chain.elements.size() == 1) {
            // Direct global variable access
            info.type = MemoryAccessInfo::GLOBAL_VARIABLE;
            info.symbol_name = first_elem.symbol_name;
            
        } else if (first_elem.type == PointerChainElement::IRQ_HANDLER_ARG0) {
            // Access through irq parameter (usually rare)
            info.type = MemoryAccessInfo::IRQ_HANDLER_IRQ_ACCESS;
            info.symbol_name = "irq_param";
            
        } else if (first_elem.type == PointerChainElement::IRQ_HANDLER_ARG1) {
            // Access through dev_id parameter (most common IRQ handler access pattern)
            if (chain.elements.size() == 1) {
                // Direct access to dev_id parameter itself
                info.type = MemoryAccessInfo::IRQ_HANDLER_DEV_ID_ACCESS;
                info.symbol_name = "dev_id_param";
            } else {
                // Access through dev_id pointer chain
                info.type = MemoryAccessInfo::POINTER_CHAIN_ACCESS;
                
                // Build detailed symbol name for fuzzing use
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
                
                // Set final accessed struct info
                if (last_elem.type == PointerChainElement::STRUCT_FIELD_DEREF) {
                    info.struct_type_name = last_elem.struct_type_name;
                    info.offset = last_elem.offset;
                }
            }
            
        } else if (chain.elements.size() > 1) {
            // Other multi-level pointer chain access
            info.type = MemoryAccessInfo::POINTER_CHAIN_ACCESS;
            
            // Set final accessed struct info
            if (last_elem.type == PointerChainElement::STRUCT_FIELD_DEREF) {
                info.struct_type_name = last_elem.struct_type_name;
                info.offset = last_elem.offset;
            } else if (last_elem.type == PointerChainElement::ARRAY_INDEX_DEREF) {
                info.type = MemoryAccessInfo::ARRAY_ELEMENT;
                info.offset = last_elem.offset;
            }
            
            // Build symbol name
            info.symbol_name = chain.toString();
            
        } else {
            // Single element of other types
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
    
    // Use is_irq_handler parameter to adjust confidence for IRQ-specific accesses
    if (is_irq_handler && (info.type == MemoryAccessInfo::IRQ_HANDLER_DEV_ID_ACCESS ||
                          info.type == MemoryAccessInfo::IRQ_HANDLER_IRQ_ACCESS)) {
        info.confidence = std::min(info.confidence + 10, 100); // Boost confidence in IRQ context
    }
    
    return info;
}

MemoryAccessInfo MemoryAccessAnalyzer::analyzeGEPInstruction(GetElementPtrInst *GEP) {
    MemoryAccessInfo info;
    
    if (!GEP) {
        return info;
    }
    
    Type *source_type = GEP->getSourceElementType();
    if (auto *struct_type = dyn_cast<StructType>(source_type)) {
        info.type = MemoryAccessInfo::STRUCT_FIELD_ACCESS;
        info.struct_type_name = struct_type->getName().str();
        
        // Calculate field offset
        if (GEP->getNumOperands() >= 3) {
            if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                info.offset = CI->getSExtValue();
                info.confidence = 90;
                
                // Get field type info
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
        
        // Get array index
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
    
    if (!GV) {
        return info;
    }
    
    info.type = MemoryAccessInfo::GLOBAL_VARIABLE;
    info.symbol_name = GV->getName().str();
    info.confidence = 95;
    
    if (DL) {
        info.access_size = DL->getTypeStoreSize(GV->getValueType());
    }
    
    // Create simple pointer chain
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
    // Keep old simple analysis method as fallback
    MemoryAccessInfo info;
    info.is_write = is_write;
    
    if (!ptr) {
        return info;
    }
    
    if (auto *GV = dyn_cast<GlobalVariable>(ptr)) {
        return analyzeGlobalVariable(GV);
        
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(ptr)) {
        return analyzeGEPInstruction(GEP);
        
    } else if (auto *CI = dyn_cast<ConstantInt>(ptr)) {
        info.type = MemoryAccessInfo::CONSTANT_ADDRESS;
        info.offset = CI->getSExtValue();
        info.confidence = 100;
        
    } else if (isa<Argument>(ptr)) {
        info.type = MemoryAccessInfo::IRQ_HANDLER_DEV_ID_ACCESS; // Assume dev_id
        info.confidence = 60;
    } else {
        info.type = MemoryAccessInfo::INDIRECT_ACCESS;
        info.confidence = 30;
    }
    
    return info;
}
