//===- DataStructures.cpp - Data Structures Implementation --------------===//

#include "DataStructures.h"

// 为了更好的调试和日志输出，提供各种枚举的字符串转换函数

const char* getAccessTypeName(MemoryAccessInfo::AccessType type) {
    switch (type) {
        case MemoryAccessInfo::GLOBAL_VARIABLE:
            return "GLOBAL_VARIABLE";
        case MemoryAccessInfo::STRUCT_FIELD_ACCESS:
            return "STRUCT_FIELD_ACCESS";
        case MemoryAccessInfo::ARRAY_ELEMENT:
            return "ARRAY_ELEMENT";
        case MemoryAccessInfo::IRQ_HANDLER_DEV_ID_ACCESS:
            return "IRQ_HANDLER_DEV_ID_ACCESS";
        case MemoryAccessInfo::IRQ_HANDLER_IRQ_ACCESS:
            return "IRQ_HANDLER_IRQ_ACCESS";
        case MemoryAccessInfo::CONSTANT_ADDRESS:
            return "CONSTANT_ADDRESS";
        case MemoryAccessInfo::INDIRECT_ACCESS:
            return "INDIRECT_ACCESS";
        case MemoryAccessInfo::POINTER_CHAIN_ACCESS:
            return "POINTER_CHAIN_ACCESS";
        default:
            return "UNKNOWN";
    }
}

// 重载版本，接受int类型（用于JSON输出）
const char* getAccessTypeName(int type) {
    return getAccessTypeName(static_cast<MemoryAccessInfo::AccessType>(type));
}

const char* getPointerChainElementTypeName(PointerChainElement::ElementType type) {
    switch (type) {
        case PointerChainElement::GLOBAL_VAR_BASE:
            return "GLOBAL_VAR_BASE";
        case PointerChainElement::IRQ_HANDLER_ARG0:
            return "IRQ_HANDLER_ARG0";
        case PointerChainElement::IRQ_HANDLER_ARG1:
            return "IRQ_HANDLER_ARG1";
        case PointerChainElement::STRUCT_FIELD_DEREF:
            return "STRUCT_FIELD_DEREF";
        case PointerChainElement::ARRAY_INDEX_DEREF:
            return "ARRAY_INDEX_DEREF";
        case PointerChainElement::DIRECT_LOAD:
            return "DIRECT_LOAD";
        case PointerChainElement::CONSTANT_OFFSET:
            return "CONSTANT_OFFSET";
        default:
            return "UNKNOWN";
    }
}

// 重载版本，接受int类型（用于JSON输出）
const char* getPointerChainElementTypeName(int type) {
    return getPointerChainElementTypeName(static_cast<PointerChainElement::ElementType>(type));
}

// 用于fuzzing的实用函数
bool MemoryAccessInfo::isDeviceRelatedAccess() const {
    return type == IRQ_HANDLER_DEV_ID_ACCESS || 
           (type == POINTER_CHAIN_ACCESS && 
            !pointer_chain.elements.empty() &&
            pointer_chain.elements[0].type == PointerChainElement::IRQ_HANDLER_ARG1);
}

bool MemoryAccessInfo::isHighConfidenceAccess() const {
    return confidence >= 80;
}

bool MemoryAccessInfo::isWriteAccess() const {
    return is_write;
}

std::string MemoryAccessInfo::getFuzzingTargetDescription() const {
    if (isDeviceRelatedAccess()) {
        return "DEV_ID_ACCESS: " + chain_description;
    } else if (type == GLOBAL_VARIABLE) {
        return "GLOBAL_VAR: " + symbol_name;
    } else if (type == POINTER_CHAIN_ACCESS) {
        return "CHAIN_ACCESS: " + chain_description;
    } else {
        return "OTHER_ACCESS: " + symbol_name;
    }
}

// 为间接调用分析提供的实用函数
size_t IndirectCallAnalysis::getTotalPossibleTargets() const {
    return fp_analysis.possible_targets.size();
}

size_t IndirectCallAnalysis::getHighConfidenceTargets() const {
    size_t count = 0;
    for (const auto& target : fp_analysis.possible_targets) {
        if (target.confidence >= 80) {
            count++;
        }
    }
    return count;
}

std::vector<Function*> IndirectCallAnalysis::getMostLikelyTargets(int min_confidence) const {
    std::vector<Function*> likely_targets;
    for (const auto& target : fp_analysis.possible_targets) {
        if (target.confidence >= min_confidence) {
            likely_targets.push_back(target.target_function);
        }
    }
    return likely_targets;
}
