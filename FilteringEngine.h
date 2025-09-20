//===- FilteringEngine.h - Advanced Memory Access Filtering Engine -----===//

#ifndef IRQ_ANALYSIS_FILTERING_ENGINE_H
#define IRQ_ANALYSIS_FILTERING_ENGINE_H

#include "DataStructures.h"
#include <vector>
#include <string>
#include <set>

//===----------------------------------------------------------------------===//
// 过滤配置枚举
//===----------------------------------------------------------------------===//

enum class FilteringLevel {
    NONE,           // 不过滤任何访问
    BASIC,          // 过滤明显的编译器生成符号
    MODERATE,       // 过滤本地计算和低置信度访问
    STRICT,         // 只保留全局变量和结构体访问
    FUZZING_FOCUS   // 专注于模糊测试目标
};

struct FilteringConfig {
    FilteringLevel level;
    bool include_constant_addresses;    // 是否包含常量地址访问
    bool include_array_accesses;       // 是否包含数组访问
    bool include_dev_id_chains;        // 是否包含dev_id指针链
    int min_confidence_threshold;      // 最低置信度阈值
    std::set<std::string> symbol_whitelist;  // 符号白名单
    std::set<std::string> symbol_blacklist;  // 符号黑名单
    
    FilteringConfig() : 
        level(FilteringLevel::MODERATE),
        include_constant_addresses(true),
        include_array_accesses(true),
        include_dev_id_chains(true),
        min_confidence_threshold(50) {}
};

//===----------------------------------------------------------------------===//
// 过滤统计信息
//===----------------------------------------------------------------------===//

struct FilteringStats {
    size_t total_accesses;
    size_t filtered_local_computation;
    size_t filtered_low_confidence;
    size_t filtered_compiler_symbols;
    size_t filtered_blacklisted;
    size_t remaining_accesses;
    
    struct CategoryStats {
        size_t global_variables;
        size_t struct_fields;
        size_t dev_id_chains;
        size_t constant_addresses;
        size_t array_elements;
    } categories;
    
    FilteringStats() : total_accesses(0), filtered_local_computation(0),
                      filtered_low_confidence(0), filtered_compiler_symbols(0),
                      filtered_blacklisted(0), remaining_accesses(0) {
        memset(&categories, 0, sizeof(categories));
    }
    
    void print() const;
};

//===----------------------------------------------------------------------===//
// 过滤引擎类
//===----------------------------------------------------------------------===//

class FilteringEngine {
private:
    FilteringConfig config;
    FilteringStats stats;
    
    /// 检查是否是编译器生成的符号
    bool isCompilerGeneratedSymbol(const std::string& symbol) const;
    
    /// 检查是否在黑名单中
    bool isBlacklisted(const MemoryAccessInfo& access) const;
    
    /// 检查是否在白名单中
    bool isWhitelisted(const MemoryAccessInfo& access) const;
    
    /// 根据过滤级别判断是否保留访问
    bool shouldKeepByLevel(const MemoryAccessInfo& access) const;
    
    /// 更新统计信息
    void updateStats(const MemoryAccessInfo& access, bool kept);
    
public:
    FilteringEngine(const FilteringConfig& cfg) : config(cfg) {}
    
    /// 过滤单个内存访问
    bool shouldKeepAccess(const MemoryAccessInfo& access);
    
    /// 过滤内存访问列表
    std::vector<MemoryAccessInfo> filterAccesses(const std::vector<MemoryAccessInfo>& accesses);
    
    /// 过滤整个分析结果
    void filterAnalysis(InterruptHandlerAnalysis& analysis);
    
    /// 获取过滤统计信息
    const FilteringStats& getStats() const { return stats; }
    
    /// 重置统计信息
    void resetStats() { stats = FilteringStats(); }
    
    /// 设置配置
    void setConfig(const FilteringConfig& cfg) { config = cfg; }
    
    /// 获取配置
    const FilteringConfig& getConfig() const { return config; }
};

//===----------------------------------------------------------------------===//
// 预定义的过滤配置
//===----------------------------------------------------------------------===//

class FilteringConfigs {
public:
    /// 获取无过滤配置
    static FilteringConfig getNoFilteringConfig();
    
    /// 获取基础过滤配置
    static FilteringConfig getBasicFilteringConfig();
    
    /// 获取适度过滤配置（默认推荐）
    static FilteringConfig getModerateFilteringConfig();
    
    /// 获取严格过滤配置
    static FilteringConfig getStrictFilteringConfig();
    
    /// 获取模糊测试专用配置
    static FilteringConfig getFuzzingFocusConfig();
    
    /// 根据字符串获取配置
    static FilteringConfig getConfigByName(const std::string& name);
    
    /// 获取所有可用的配置名称
    static std::vector<std::string> getAvailableConfigNames();
    
    /// 打印配置说明
    static void printConfigHelp();
};

#endif // IRQ_ANALYSIS_FILTERING_ENGINE_H
