//===- TypeAnalyzer.cpp - 类型分析函数实现 -------------------------------===//

#include "SVFInterruptAnalyzer.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// 类型名称获取函数
//===----------------------------------------------------------------------===//

std::string SVFInterruptAnalyzer::getTypeName(llvm::Type* type) {
    if (!type) {
        return "unknown";
    }
    
    std::string type_name;
    llvm::raw_string_ostream stream(type_name);
    type->print(stream);
    stream.flush();
    
    // 清理类型名称
    if (type_name.find("struct.") == 0) {
        type_name = type_name.substr(7); // 移除 "struct." 前缀
    }
    if (type_name.find("class.") == 0) {
        type_name = type_name.substr(6); // 移除 "class." 前缀
    }
    
    return type_name;
}

std::string SVFInterruptAnalyzer::getStructName(llvm::Type* type) {
    if (!type) {
        return "unknown";
    }
    
    if (auto* structType = dyn_cast<StructType>(type)) {
        if (structType->hasName()) {
            std::string name = structType->getName().str();
            // 清理结构体名称
            if (name.find("struct.") == 0) {
                name = name.substr(7);
            }
            if (name.find("class.") == 0) {
                name = name.substr(6);
            }
            return name;
        } else {
            return "anonymous_struct";
        }
    }
    
    // 如果是指针类型，尝试获取指向的结构体名称
    if (auto* ptrType = dyn_cast<PointerType>(type)) {
        Type* elementType = ptrType->getPointerElementType();
        if (auto* structType = dyn_cast<StructType>(elementType)) {
            if (structType->hasName()) {
                std::string name = structType->getName().str();
                if (name.find("struct.") == 0) {
                    name = name.substr(7);
                }
                if (name.find("class.") == 0) {
                    name = name.substr(6);
                }
                return name + "*";
            }
        }
    }
    
    return getTypeName(type);
}

//===----------------------------------------------------------------------===//
// 字段信息获取函数
//===----------------------------------------------------------------------===//

std::string SVFInterruptAnalyzer::getFieldName(llvm::Type* structType, unsigned index) {
    if (!structType) {
        return "field_" + std::to_string(index);
    }
    
    auto* st = dyn_cast<StructType>(structType);
    if (!st) {
        return "field_" + std::to_string(index);
    }
    
    // 检查索引是否有效
    if (index >= st->getNumElements()) {
        return "invalid_field_" + std::to_string(index);
    }
    
    // 尝试从调试信息获取字段名称
    // 由于没有直接的方法从LLVM类型获取字段名称，我们使用通用名称
    std::string struct_name = getStructName(structType);
    return struct_name + "_field_" + std::to_string(index);
}

size_t SVFInterruptAnalyzer::getFieldOffset(llvm::StructType* structType, unsigned index) {
    if (!structType || index >= structType->getNumElements()) {
        return 0;
    }
    
    // 创建一个临时的DataLayout来计算偏移
    // 注意：这是一个简化的实现，实际应用中可能需要更精确的计算
    try {
        // 使用默认的数据布局
        DataLayout DL("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
        
        // 计算字段偏移
        const StructLayout* layout = DL.getStructLayout(structType);
        return layout->getElementOffset(index);
        
    } catch (...) {
        // 如果计算失败，返回估算值
        size_t offset = 0;
        for (unsigned i = 0; i < index && i < structType->getNumElements(); ++i) {
            Type* fieldType = structType->getElementType(i);
            if (fieldType->isSized()) {
                // 简单的大小估算
                if (fieldType->isIntegerTy()) {
                    offset += (fieldType->getIntegerBitWidth() + 7) / 8;
                } else if (fieldType->isPointerTy()) {
                    offset += 8; // 假设64位指针
                } else if (fieldType->isFloatingPointTy()) {
                    if (fieldType->isFloatTy()) {
                        offset += 4;
                    } else if (fieldType->isDoubleTy()) {
                        offset += 8;
                    }
                } else {
                    offset += 8; // 默认大小
                }
            }
        }
        return offset;
    }
}

//===----------------------------------------------------------------------===//
// 置信度计算函数
//===----------------------------------------------------------------------===//

double SVFInterruptAnalyzer::calculateConfidence(const InterruptHandlerResult& result) {
    double confidence = 0.0;
    double max_score = 100.0;
    
    // 基础分析完整性 (30分)
    if (result.total_instructions > 0) confidence += 10.0;
    if (result.total_basic_blocks > 0) confidence += 10.0;
    if (!result.direct_function_calls.empty()) confidence += 10.0;
    
    // 内存访问分析 (25分)
    if (result.memory_read_operations > 0) confidence += 8.0;
    if (result.memory_write_operations > 0) confidence += 8.0;
    if (!result.memory_writes.empty()) confidence += 9.0;
    
    // 数据结构分析 (25分)
    if (!result.data_structure_accesses.empty()) {
        confidence += 10.0;
        
        // 检查完整路径的质量
        size_t complete_paths = 0;
        for (const auto& access : result.data_structure_accesses) {
            if (!access.full_path.empty() && access.full_path != access.field_name) {
                complete_paths++;
            }
        }
        
        if (complete_paths > 0) {
            confidence += 10.0;
        }
        
        // 检查访问链的复杂性
        if (complete_paths > result.data_structure_accesses.size() / 2) {
            confidence += 5.0;
        }
    }
    
    // 中断处理特征 (20分)
    if (result.has_device_access) confidence += 7.0;
    if (result.has_irq_operations) confidence += 7.0;
    if (result.has_work_queue_ops) confidence += 6.0;
    
    // 确保不超过最大分数
    return std::min(confidence, max_score);
}
