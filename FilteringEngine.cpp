//===- FilteringEngine.cpp - Advanced Filtering Engine Implementation ---===//

#include "FilteringEngine.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

//===----------------------------------------------------------------------===//
// FilteringStats Implementation
//===----------------------------------------------------------------------===//

void FilteringStats::print() const {
    outs() << "\n=== Memory Access Filtering Statistics ===\n";
    outs() << "Total accesses found: " << total_accesses << "\n";
    outs() << "Filtered accesses:\n";
    outs() << "  Local computation: " << filtered_local_computation << "\n";
    outs() << "  Low confidence: " << filtered_low_confidence << "\n";
    outs() << "  Compiler symbols: " << filtered_compiler_symbols << "\n";
    outs() << "  Blacklisted: " << filtered_blacklisted << "\n";
    outs() << "Remaining accesses: " << remaining_accesses << "\n";
    outs() << "\nAccess categories (after filtering):\n";
    outs() << "  Global variables: " << categories.global_variables << "\n";
    outs() << "  Struct fields: " << categories.struct_fields << "\n";
    outs() << "  Dev_id chains: " << categories.dev_id_chains << "\n";
    outs() << "  Constant addresses: " << categories.constant_addresses << "\n";
    outs() << "  Array elements: " << categories.array_elements << "\n";
}

//===----------------------------------------------------------------------===//
// FilteringEngine Implementation
//===----------------------------------------------------------------------===//

bool FilteringEngine::isCompilerGeneratedSymbol(const std::string& symbol) const {
    if (symbol.empty()) {
        return false;
    }
    
    // 扩展的编译器符号检测
    static const std::vector<std::string> compiler_patterns = {
        "__llvm_gcov_ctr",      // LLVM coverage counters
        "__llvm_gcda_",         // LLVM coverage data
        "__llvm_gcno_",         // LLVM coverage notes
        "__llvm_prf_",          // LLVM profiling
        "__sanitizer_cov_",     // Sanitizer coverage
        "__asan_",              // AddressSanitizer
        "__msan_",              // MemorySanitizer
        "__tsan_",              // ThreadSanitizer
        "__ubsan_",             // UndefinedBehaviorSanitizer
        "__stack_chk_",         // Stack protection
        "__profile_",           // Profile instrumentation
        ".L",                   // Local labels
        ".str",                 // String constants
        "local_computation",    // Our own classification
        "tmp",                  // Temporary variables
        "__cfi_",              // Control Flow Integrity
        "__sancov_",           // Sanitizer coverage (alternative)
    };
    
    for (const auto& pattern : compiler_patterns) {
        if (symbol.find(pattern) == 0) {
            return true;
        }
    }
    
    return false;
}

bool FilteringEngine::isBlacklisted(const MemoryAccessInfo& access) const {
    // 检查符号名
    if (!access.symbol_name.empty() && 
        config.symbol_blacklist.count(access.symbol_name) > 0) {
        return true;
    }
    
    // 检查结构体类型名
    if (!access.struct_type_name.empty() && 
        config.symbol_blacklist.count(access.struct_type_name) > 0) {
        return true;
    }
    
    // 检查指针链中的符号
    for (const auto& elem : access.pointer_chain.elements) {
        if (!elem.symbol_name.empty() && 
            config.symbol_blacklist.count(elem.symbol_name) > 0) {
            return true;
        }
        if (!elem.struct_type_name.empty() && 
            config.symbol_blacklist.count(elem.struct_type_name) > 0) {
            return true;
        }
    }
    
    return false;
}

bool FilteringEngine::isWhitelisted(const MemoryAccessInfo& access) const {
    if (config.symbol_whitelist.empty()) {
        return false; // 没有白名单时，不特别处理
    }
    
    // 检查符号名
    if (!access.symbol_name.empty() && 
        config.symbol_whitelist.count(access.symbol_name) > 0) {
        return true;
    }
    
    // 检查结构体类型名
    if (!access.struct_type_name.empty() && 
        config.symbol_whitelist.count(access.struct_type_name) > 0) {
        return true;
    }
    
    // 检查指针链中的符号
    for (const auto& elem : access.pointer_chain.elements) {
        if (!elem.symbol_name.empty() && 
            config.symbol_whitelist.count(elem.symbol_name) > 0) {
            return true;
        }
    }
    
    return false;
}

bool FilteringEngine::shouldKeepByLevel(const MemoryAccessInfo& access) const {
    switch (config.level) {
        case FilteringLevel::NONE:
            return true;
            
        case FilteringLevel::BASIC:
            // 只过滤明显的编译器符号
            return !isCompilerGeneratedSymbol(access.symbol_name) &&
                   !isCompilerGeneratedSymbol(access.chain_description);
            
        case FilteringLevel::MODERATE:
            // 过滤编译器符号和低置信度访问
            if (isCompilerGeneratedSymbol(access.symbol_name) ||
                isCompilerGeneratedSymbol(access.chain_description)) {
                return false;
            }
            if (access.confidence < config.min_confidence_threshold) {
                return false;
            }
            if (access.type == MemoryAccessInfo::INDIRECT_ACCESS &&
                access.symbol_name == "local_computation") {
                return false;
            }
            return true;
            
        case FilteringLevel::STRICT:
            // 只保留明确的全局变量和结构体访问
            switch (access.type) {
                case MemoryAccessInfo::GLOBAL_VARIABLE:
                    return !isCompilerGeneratedSymbol(access.symbol_name);
                case MemoryAccessInfo::STRUCT_FIELD_ACCESS:
                    return true;
                case MemoryAccessInfo::IRQ_HANDLER_DEV_ID_ACCESS:
                    return config.include_dev_id_chains;
                case MemoryAccessInfo::CONSTANT_ADDRESS:
                    return config.include_constant_addresses;
                case MemoryAccessInfo::ARRAY_ELEMENT:
                    return config.include_array_accesses;
                case MemoryAccessInfo::POINTER_CHAIN_ACCESS:
                    // 只保留以全局变量或dev_id开始的指针链
                    if (!access.pointer_chain.elements.empty()) {
                        const auto& first = access.pointer_chain.elements[0];
                        return (first.type == PointerChainElement::GLOBAL_VAR_BASE &&
                                !isCompilerGeneratedSymbol(first.symbol_name)) ||
                               (first.type == PointerChainElement::IRQ_HANDLER_ARG1 &&
                                config.include_dev_id_chains);
                    }
                    return false;
                default:
                    return false;
            }
            
        case FilteringLevel::FUZZING_FOCUS:
            // 专注于模糊测试相关的访问
            if (access.isDeviceRelatedAccess()) {
                return true;
            }
            if (access.type == MemoryAccessInfo::GLOBAL_VARIABLE &&
                access.is_write && 
                !isCompilerGeneratedSymbol(access.symbol_name)) {
                return true;
            }
            if (access.type == MemoryAccessInfo::STRUCT_FIELD_ACCESS &&
                access.is_write) {
                return true;
            }
            if (access.type == MemoryAccessInfo::CONSTANT_ADDRESS) {
                return config.include_constant_addresses;
            }
            return false;
    }
    
    return false;
}

void FilteringEngine::updateStats(const MemoryAccessInfo& access, bool kept) {
    stats.total_accesses++;
    
    if (!kept) {
        // 统计过滤原因
        if (access.symbol_name == "local_computation" ||
            access.chain_description.find("local_computation") != std::string::npos) {
            stats.filtered_local_computation++;
        } else if (access.confidence < config.min_confidence_threshold) {
            stats.filtered_low_confidence++;
        } else if (isCompilerGeneratedSymbol(access.symbol_name) ||
                   isCompilerGeneratedSymbol(access.chain_description)) {
            stats.filtered_compiler_symbols++;
        } else if (isBlacklisted(access)) {
            stats.filtered_blacklisted++;
        }
    } else {
        // 统计保留的访问类型
        stats.remaining_accesses++;
        
        switch (access.type) {
            case MemoryAccessInfo::GLOBAL_VARIABLE:
                stats.categories.global_variables++;
                break;
            case MemoryAccessInfo::STRUCT_FIELD_ACCESS:
            case MemoryAccessInfo::POINTER_CHAIN_ACCESS:
                stats.categories.struct_fields++;
                break;
            case MemoryAccessInfo::IRQ_HANDLER_DEV_ID_ACCESS:
                stats.categories.dev_id_chains++;
                break;
            case MemoryAccessInfo::CONSTANT_ADDRESS:
                stats.categories.constant_addresses++;
                break;
            case MemoryAccessInfo::ARRAY_ELEMENT:
                stats.categories.array_elements++;
                break;
            default:
                break;
        }
    }
}

bool FilteringEngine::shouldKeepAccess(const MemoryAccessInfo& access) {
    // 白名单优先
    if (isWhitelisted(access)) {
        updateStats(access, true);
        return true;
    }
    
    // 黑名单检查
    if (isBlacklisted(access)) {
        updateStats(access, false);
        return false;
    }
    
    // 基于级别的过滤
    bool keep = shouldKeepByLevel(access);
    updateStats(access, keep);
    return keep;
}

std::vector<MemoryAccessInfo> FilteringEngine::filterAccesses(const std::vector<MemoryAccessInfo>& accesses) {
    std::vector<MemoryAccessInfo> filtered;
    
    for (const auto& access : accesses) {
        if (shouldKeepAccess(access)) {
            filtered.push_back(access);
        }
    }
    
    return filtered;
}

void FilteringEngine::filterAnalysis(InterruptHandlerAnalysis& analysis) {
    // 过滤直接内存访问
    analysis.memory_accesses = filterAccesses(analysis.memory_accesses);
    
    // 过滤总内存访问（包括间接调用）
    analysis.total_memory_accesses = filterAccesses(analysis.total_memory_accesses);
    
    // 更新间接调用分析中的聚合访问
    for (auto& indirect : analysis.indirect_call_analyses) {
        indirect.aggregated_accesses = filterAccesses(indirect.aggregated_accesses);
    }
    
    // 重新构建访问的符号集合
    analysis.accessed_global_vars.clear();
    analysis.accessed_struct_types.clear();
    
    for (const auto& access : analysis.total_memory_accesses) {
        if (access.type == MemoryAccessInfo::GLOBAL_VARIABLE) {
            analysis.accessed_global_vars.insert(access.symbol_name);
        }
        
        if (!access.struct_type_name.empty()) {
            analysis.accessed_struct_types.insert(access.struct_type_name);
        }
        
        for (const auto& elem : access.pointer_chain.elements) {
            if (!elem.struct_type_name.empty()) {
                analysis.accessed_struct_types.insert(elem.struct_type_name);
            }
        }
    }
}

//===----------------------------------------------------------------------===//
// FilteringConfigs Implementation
//===----------------------------------------------------------------------===//

FilteringConfig FilteringConfigs::getNoFilteringConfig() {
    FilteringConfig config;
    config.level = FilteringLevel::NONE;
    config.min_confidence_threshold = 0;
    return config;
}

FilteringConfig FilteringConfigs::getBasicFilteringConfig() {
    FilteringConfig config;
    config.level = FilteringLevel::BASIC;
    config.min_confidence_threshold = 30;
    config.include_constant_addresses = true;
    config.include_array_accesses = true;
    config.include_dev_id_chains = true;
    return config;
}

FilteringConfig FilteringConfigs::getModerateFilteringConfig() {
    FilteringConfig config;
    config.level = FilteringLevel::MODERATE;
    config.min_confidence_threshold = 50;
    config.include_constant_addresses = true;
    config.include_array_accesses = true;
    config.include_dev_id_chains = true;
    
    // 添加常见的不相关符号到黑名单
    config.symbol_blacklist.insert("unknown");
    config.symbol_blacklist.insert("local_computation");
    config.symbol_blacklist.insert("tmp");
    
    return config;
}

FilteringConfig FilteringConfigs::getStrictFilteringConfig() {
    FilteringConfig config;
    config.level = FilteringLevel::STRICT;
    config.min_confidence_threshold = 60;
    config.include_constant_addresses = true;
    config.include_array_accesses = true;
    config.include_dev_id_chains = true;
    
    // 扩展黑名单
    config.symbol_blacklist.insert("unknown");
    config.symbol_blacklist.insert("local_computation");
    config.symbol_blacklist.insert("tmp");
    config.symbol_blacklist.insert("func_arg_");
    config.symbol_blacklist.insert("complex_computation");
    
    return config;
}

FilteringConfig FilteringConfigs::getFuzzingFocusConfig() {
    FilteringConfig config;
    config.level = FilteringLevel::FUZZING_FOCUS;
    config.min_confidence_threshold = 70;
    config.include_constant_addresses = true;
    config.include_array_accesses = false;
    config.include_dev_id_chains = true;
    
    // 模糊测试相关的白名单
    config.symbol_whitelist.insert("pci_dev");
    config.symbol_whitelist.insert("net_device");
    config.symbol_whitelist.insert("irq_desc");
    config.symbol_whitelist.insert("tasklet_struct");
    config.symbol_whitelist.insert("work_struct");
    config.symbol_whitelist.insert("timer_list");
    config.symbol_whitelist.insert("sk_buff");
    config.symbol_whitelist.insert("device");
    
    // 扩展黑名单
    config.symbol_blacklist.insert("unknown");
    config.symbol_blacklist.insert("local_computation");
    config.symbol_blacklist.insert("tmp");
    config.symbol_blacklist.insert("func_arg_");
    config.symbol_blacklist.insert("complex_computation");
    config.symbol_blacklist.insert("arithmetic_offset");
    config.symbol_blacklist.insert("dynamic_address");
    
    return config;
}

FilteringConfig FilteringConfigs::getConfigByName(const std::string& name) {
    if (name == "none" || name == "off") {
        return getNoFilteringConfig();
    } else if (name == "basic") {
        return getBasicFilteringConfig();
    } else if (name == "moderate" || name == "default") {
        return getModerateFilteringConfig();
    } else if (name == "strict") {
        return getStrictFilteringConfig();
    } else if (name == "fuzzing" || name == "fuzz") {
        return getFuzzingFocusConfig();
    } else {
        // 默认返回适度配置
        return getModerateFilteringConfig();
    }
}

std::vector<std::string> FilteringConfigs::getAvailableConfigNames() {
    return {
        "none", "basic", "moderate", "strict", "fuzzing"
    };
}

void FilteringConfigs::printConfigHelp() {
    outs() << "Available filtering configurations:\n";
    outs() << "  none/off    - No filtering (show all memory accesses)\n";
    outs() << "  basic       - Filter obvious compiler-generated symbols\n";
    outs() << "  moderate    - Filter local computation and low confidence (default)\n";
    outs() << "  strict      - Only global variables and struct accesses\n";
    outs() << "  fuzzing     - Focus on fuzzing-relevant targets\n";
}
