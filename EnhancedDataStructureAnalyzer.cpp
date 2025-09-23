//===- EnhancedDataStructureAnalyzer.cpp - 改进的数据结构分析器 -----------===//

#include "SVFInterruptAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// 改进的结构体名称清理
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

std::string SVFInterruptAnalyzer::getStructName(Type* type) {
    if (auto* struct_type = dyn_cast<StructType>(type)) {
        if (struct_type->hasName()) {
            std::string name = struct_type->getName().str();
            
            // 清理LLVM类型名前缀和后缀
            if (name.substr(0, 7) == "struct.") {
                name = name.substr(7);
            }
            
            // 移除尾部的数字标识符 (如 .19, .15 等)
            size_t dot_pos = name.find_last_of('.');
            if (dot_pos != std::string::npos) {
                std::string suffix = name.substr(dot_pos + 1);
                // 检查后缀是否为数字
                bool is_numeric = !suffix.empty() && 
                    std::all_of(suffix.begin(), suffix.end(), ::isdigit);
                if (is_numeric) {
                    name = name.substr(0, dot_pos);
                }
            }
            
            return name;
        } else {
            return "anonymous_struct";
        }
    }
    return "";
}

//===----------------------------------------------------------------------===//
// 新增：详细的GEP写操作分析
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::analyzeGEPWriteOperation(GetElementPtrInst* gep, MemoryWriteOperation& write_op, const std::string& func_name) {
    Value* base = gep->getPointerOperand();
    Type* source_type = gep->getSourceElementType();
    
    // 获取基础信息
    std::string base_name = "unknown";
    std::string struct_name = "";
    
    if (base->hasName()) {
        base_name = base->getName().str();
    } else if (auto* GV = dyn_cast<GlobalVariable>(base)) {
        base_name = GV->getName().str();
    }
    
    // 分析结构体类型
    if (auto* struct_type = dyn_cast<StructType>(source_type)) {
        struct_name = getStructName(struct_type);
        write_op.target_type = "struct_field";
        write_op.is_critical = true;
        
        // 分析GEP索引来获取字段信息
        StructFieldInfo field_info = analyzeGEPFieldInfo(gep, struct_type);
        
        // 构建完整的目标名称
        write_op.target_name = struct_name;
        
        // 设置增强信息
        write_op.struct_name = struct_name;
        write_op.field_name = field_info.field_name;
        write_op.field_offset = field_info.field_offset;
        write_op.field_size = field_info.field_size;
        write_op.field_type = field_info.field_type;
        write_op.full_path = base_name + "." + struct_name + "::" + field_info.field_name;
        
    } else if (source_type->isArrayTy()) {
        // 数组访问
        write_op.target_type = "array_element";
        write_op.target_name = base_name + "[array_element]";
        write_op.is_critical = true;
        
        ArrayElementInfo array_info = analyzeGEPArrayInfo(gep, source_type);
        write_op.field_name = "element_" + std::to_string(array_info.index);
        write_op.field_offset = array_info.offset;
        write_op.field_size = array_info.element_size;
        write_op.full_path = base_name + "[" + std::to_string(array_info.index) + "]";
        
    } else {
        // 其他指针类型
        write_op.target_type = "pointer_deref";
        write_op.target_name = base_name + "_deref";
        write_op.is_critical = true;
    }
}

//===----------------------------------------------------------------------===//
// 新增：结构体字段信息分析
//===----------------------------------------------------------------------===//

StructFieldInfo SVFInterruptAnalyzer::analyzeGEPFieldInfo(GetElementPtrInst* gep, StructType* struct_type) {
    StructFieldInfo info;
    info.field_name = "unknown_field";
    info.field_offset = 0;
    info.field_size = 0;
    info.field_type = "unknown";
    
    if (gep->getNumIndices() >= 2) {
        auto indices = gep->indices();
        auto it = indices.begin();
        ++it; // 跳过第一个索引（通常是0）
        
        if (it != indices.end()) {
            if (auto* CI = dyn_cast<ConstantInt>(*it)) {
                unsigned field_index = CI->getZExtValue();
                
                if (field_index < struct_type->getNumElements()) {
                    info.field_name = "field_" + std::to_string(field_index);
                    info.field_offset = calculateFieldOffset(struct_type, field_index);
                    
                    Type* field_type = struct_type->getElementType(field_index);
                    info.field_type = getTypeName(field_type);
                    info.field_size = calculateTypeSize(field_type);
                    
                    // 尝试从调试信息获取真实字段名
                    // std::string real_field_name = extractRealFieldName(struct_type, field_index);
                    // if (!real_field_name.empty()) {
                    //     info.field_name = real_field_name;
                    // }
                }
            } else {
                // 动态索引
                info.field_name = "dynamic_field";
                info.field_type = "dynamic";
            }
        }
    }
    
    return info;
}

//===----------------------------------------------------------------------===//
// 新增：数组元素信息分析  
//===----------------------------------------------------------------------===//

ArrayElementInfo SVFInterruptAnalyzer::analyzeGEPArrayInfo(GetElementPtrInst* gep, Type* array_type) {
    ArrayElementInfo info;
    info.index = -1;
    info.offset = 0;
    info.element_size = 0;
    
    if (auto* array_ty = dyn_cast<ArrayType>(array_type)) {
        Type* element_type = array_ty->getElementType();
        info.element_size = calculateTypeSize(element_type);
        
        // 分析数组索引
        if (gep->getNumIndices() >= 2) {
            auto indices = gep->indices();
            auto it = indices.begin();
            ++it; // 跳过第一个索引
            
            if (it != indices.end()) {
                if (auto* CI = dyn_cast<ConstantInt>(*it)) {
                    info.index = CI->getSExtValue();
                    info.offset = info.index * info.element_size;
                }
            }
        }
    }
    
    return info;
}

//===----------------------------------------------------------------------===//
// 新增：字段偏移计算
//===----------------------------------------------------------------------===//

size_t SVFInterruptAnalyzer::calculateFieldOffset(StructType* struct_type, unsigned field_index) {
    size_t offset = 0;
    
    // 简化的偏移计算 - 在实际应用中应该使用DataLayout
    for (unsigned i = 0; i < field_index && i < struct_type->getNumElements(); ++i) {
        Type* field_type = struct_type->getElementType(i);
        size_t field_size = calculateTypeSize(field_type);
        
        // 简单的对齐处理 - 按8字节对齐
        if (offset % 8 != 0) {
            offset = (offset + 7) & ~7;
        }
        
        offset += field_size;
    }
    
    return offset;
}

//===----------------------------------------------------------------------===//
// 新增：类型大小计算
//===----------------------------------------------------------------------===//

size_t SVFInterruptAnalyzer::calculateTypeSize(Type* type) {
    if (type->isIntegerTy()) {
        return (type->getIntegerBitWidth() + 7) / 8;
    } else if (type->isFloatTy()) {
        return 4;
    } else if (type->isDoubleTy()) {
        return 8;
    } else if (type->isPointerTy()) {
        return 8;  // 64位指针
    } else if (auto* array_type = dyn_cast<ArrayType>(type)) {
        return array_type->getNumElements() * calculateTypeSize(array_type->getElementType());
    } else if (auto* struct_type = dyn_cast<StructType>(type)) {
        size_t total_size = 0;
        for (unsigned i = 0; i < struct_type->getNumElements(); ++i) {
            total_size += calculateTypeSize(struct_type->getElementType(i));
        }
        return total_size;
    }
    
    return 8; // 默认大小
}

//===----------------------------------------------------------------------===//
// 新增：从调试信息提取真实字段名
//===----------------------------------------------------------------------===//

std::string SVFInterruptAnalyzer::extractRealFieldName(StructType* struct_type, unsigned field_index) {
    // 这里需要访问调试信息来获取真实的字段名
    // 暂时返回空字符串，表示使用默认的field_N命名
    
    // 在实际实现中，可能需要：
    // 1. 访问DICompositeType获取结构体调试信息
    // 2. 遍历DIDerivedType获取字段信息
    // 3. 匹配field_index和真实字段名
    
    // 对于常见的结构体，可以使用硬编码映射
    std::string struct_name = getStructName(struct_type);
    
    if (struct_name == "test_device") {
        static const char* test_device_fields[] = {
            "regs", "stats", "rx_buffers", "tx_buffers", "lock", 
            "device_list", "state", "irq_number", "callback", "work", "name", "flags"
        };
        if (field_index < sizeof(test_device_fields) / sizeof(test_device_fields[0])) {
            return test_device_fields[field_index];
        }
    } else if (struct_name == "buffer_info") {
        static const char* buffer_info_fields[] = {
            "data_ptr", "size", "used", "next", "ref_count"
        };
        if (field_index < sizeof(buffer_info_fields) / sizeof(buffer_info_fields[0])) {
            return buffer_info_fields[field_index];
        }
    } else if (struct_name == "device_regs") {
        static const char* device_regs_fields[] = {
            "control", "status", "data", "irq_mask", "dma_addr"
        };
        if (field_index < sizeof(device_regs_fields) / sizeof(device_regs_fields[0])) {
            return device_regs_fields[field_index];
        }
    } else if (struct_name == "irq_stats") {
        static const char* irq_stats_fields[] = {
            "total_irqs", "error_irqs", "spurious_irqs", "last_error_code"
        };
        if (field_index < sizeof(irq_stats_fields) / sizeof(irq_stats_fields[0])) {
            return irq_stats_fields[field_index];
        }
    }
    
    return ""; // 返回空字符串表示使用默认命名
}

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


std::string SVFInterruptAnalyzer::getFieldName(Type* struct_type, unsigned field_index) {
    // 由于LLVM IR中通常不保留字段名，我们生成描述性名称
    return "field_" + std::to_string(field_index);
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