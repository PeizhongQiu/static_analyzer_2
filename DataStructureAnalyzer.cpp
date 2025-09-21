//===- DataStructureAnalyzer.cpp - 基于调用图的数据结构分析器 -------------===//

#include "SVFInterruptAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// 单函数数据结构分析
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::analyzeDataStructuresInFunction(Function* function, 
                                                           std::map<std::string, DataStructureAccess>& unique_accesses) {
    std::string func_name = function->getName().str();
    
    for (auto& BB : *function) {
        for (auto& I : BB) {
            if (auto* GEP = dyn_cast<GetElementPtrInst>(&I)) {
                DataStructureAccess access = analyzeStructAccess(GEP);
                if (!access.struct_name.empty()) {
                    // 添加函数信息到访问模式中
                    access.access_pattern += " (in " + func_name + ")";
                    
                    // 使用结构名和字段名作为唯一键，但保留函数信息
                    std::string key = access.struct_name + "." + access.field_name;
                    
                    if (unique_accesses.find(key) == unique_accesses.end()) {
                        unique_accesses[key] = access;
                    } else {
                        // 如果已存在，更新访问模式信息
                        auto& existing = unique_accesses[key];
                        if (existing.access_pattern.find(func_name) == std::string::npos) {
                            existing.access_pattern += ", " + func_name;
                        }
                    }
                }
            }
        }
    }
}

DataStructureAccess SVFInterruptAnalyzer::analyzeStructAccess(const GetElementPtrInst* gep) {
    DataStructureAccess access;
    
    Type* source_type = gep->getSourceElementType();
    if (auto* struct_type = dyn_cast<StructType>(source_type)) {
        access.struct_name = getStructName(struct_type);
        
        // 分析字段访问
        if (gep->getNumIndices() >= 2) {
            auto indices = gep->indices();
            auto it = indices.begin();
            ++it; // 跳过第一个索引（通常是0）
            
            if (it != indices.end()) {
                if (auto* CI = dyn_cast<ConstantInt>(*it)) {
                    unsigned field_index = CI->getZExtValue();
                    
                    if (field_index < struct_type->getNumElements()) {
                        access.field_name = getFieldName(struct_type, field_index);
                        access.offset = getFieldOffset(struct_type, field_index);
                        
                        Type* field_type = struct_type->getElementType(field_index);
                        access.field_type = getTypeName(field_type);
                        access.is_pointer_field = field_type->isPointerTy();
                        
                        // 分析访问模式
                        access.access_pattern = analyzeAccessPattern(gep, field_type);
                    }
                } else {
                    // 动态索引访问
                    access.field_name = "dynamic_field";
                    access.field_type = "unknown";
                    access.access_pattern = "dynamic_access";
                }
            }
        }
        
        if (access.access_pattern.empty()) {
            access.access_pattern = "struct_field_access";
        }
    } else if (source_type->isArrayTy()) {
        // 数组访问
        access.struct_name = "array_" + getTypeName(source_type);
        access.field_name = "element";
        access.field_type = getTypeName(source_type->getArrayElementType());
        access.access_pattern = "array_element_access";
        access.is_pointer_field = source_type->getArrayElementType()->isPointerTy();
    }
    
    return access;
}

std::string SVFInterruptAnalyzer::analyzeAccessPattern(const GetElementPtrInst* gep, Type* field_type) {
    std::string pattern = "struct_field_access";
    
    // 检查是否是嵌套结构访问
    if (gep->getNumIndices() > 2) {
        pattern = "nested_struct_access";
    }
    
    // 检查字段类型特征
    if (field_type->isPointerTy()) {
        pattern += "_pointer";
    } else if (field_type->isArrayTy()) {
        pattern += "_array";
    } else if (field_type->isStructTy()) {
        pattern += "_struct";
    }
    
    // 检查使用上下文
    bool has_read = false, has_write = false;
    for (auto* user : gep->users()) {
        if (isa<LoadInst>(user)) {
            has_read = true;
        } else if (isa<StoreInst>(user)) {
            has_write = true;
        }
    }
    
    if (has_write && has_read) {
        pattern += "_read_write";
    } else if (has_write) {
        pattern += "_write_only";
    } else if (has_read) {
        pattern += "_read_only";
    }
    
    return pattern;
}

std::string SVFInterruptAnalyzer::getStructName(Type* type) {
    if (auto* struct_type = dyn_cast<StructType>(type)) {
        if (struct_type->hasName()) {
            std::string name = struct_type->getName().str();
            // 清理LLVM类型名前缀
            if (name.substr(0, 7) == "struct.") {
                name = name.substr(7);
            }
            return name;
        } else {
            return "anonymous_struct_" + std::to_string(reinterpret_cast<uintptr_t>(struct_type));
        }
    }
    return "";
}

std::string SVFInterruptAnalyzer::getFieldName(Type* struct_type, unsigned field_index) {
    // 由于LLVM IR中通常不保留字段名，我们生成描述性名称
    return "field_" + std::to_string(field_index);
}

//===----------------------------------------------------------------------===//
// 类型分析辅助函数
//===----------------------------------------------------------------------===//

std::string SVFInterruptAnalyzer::getTypeName(Type* type) {
    // 检查缓存
    auto it = type_name_cache.find(type);
    if (it != type_name_cache.end()) {
        return it->second;
    }
    
    std::string name;
    if (type->isIntegerTy()) {
        name = "i" + std::to_string(type->getIntegerBitWidth());
    } else if (type->isFloatingPointTy()) {
        if (type->isFloatTy()) name = "float";
        else if (type->isDoubleTy()) name = "double";
        else name = "floating_point";
    } else if (type->isPointerTy()) {
        name = "pointer";  // 简化处理，避免递归类型问题
    } else if (auto* struct_type = dyn_cast<StructType>(type)) {
        name = "struct." + getStructName(struct_type);
    } else if (type->isArrayTy()) {
        auto* array_type = cast<ArrayType>(type);
        name = "[" + std::to_string(array_type->getNumElements()) + " x " + 
               getTypeName(array_type->getElementType()) + "]";
    } else if (type->isVoidTy()) {
        name = "void";
    } else if (type->isFunctionTy()) {
        name = "function";
    } else {
        name = "unknown_type";
    }
    
    // 缓存结果
    type_name_cache[type] = name;
    return name;
}

std::vector<std::string> SVFInterruptAnalyzer::getStructFields(StructType* struct_type) {
    std::string struct_name = getStructName(struct_type);
    
    // 检查缓存
    auto it = struct_field_cache.find(struct_name);
    if (it != struct_field_cache.end()) {
        return it->second;
    }
    
    std::vector<std::string> fields;
    for (unsigned i = 0; i < struct_type->getNumElements(); ++i) {
        std::string field_name = "field_" + std::to_string(i);
        Type* field_type = struct_type->getElementType(i);
        field_name += "_" + getTypeName(field_type);
        fields.push_back(field_name);
    }
    
    // 缓存结果
    struct_field_cache[struct_name] = fields;
    return fields;
}

size_t SVFInterruptAnalyzer::getFieldOffset(StructType* struct_type, unsigned field_index) {
    // 简化实现：返回字段索引作为偏移
    // 在实际应用中，需要使用DataLayout来计算真实偏移
    
    // 尝试估算偏移量
    size_t offset = 0;
    for (unsigned i = 0; i < field_index && i < struct_type->getNumElements(); ++i) {
        Type* field_type = struct_type->getElementType(i);
        
        // 简单的大小估算
        if (field_type->isIntegerTy()) {
            offset += (field_type->getIntegerBitWidth() + 7) / 8;
        } else if (field_type->isPointerTy()) {
            offset += 8;  // 64位指针
        } else if (field_type->isFloatTy()) {
            offset += 4;
        } else if (field_type->isDoubleTy()) {
            offset += 8;
        } else {
            offset += 8;  // 默认估算
        }
        
        // 简单的对齐处理
        if (offset % 8 != 0) {
            offset = (offset + 7) & ~7;
        }
    }
    
    return offset;
}
