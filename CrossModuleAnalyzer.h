//===- CrossModuleAnalyzer.h - 增强的跨模块分析器 with SVF Support -------===//

#ifndef IRQ_ANALYSIS_CROSS_MODULE_ANALYZER_H
#define IRQ_ANALYSIS_CROSS_MODULE_ANALYZER_H

#include "DataStructures.h"
#include "IRQHandlerIdentifier.h"
#include "MemoryAccessAnalyzer.h"
#include "InlineAsmAnalyzer.h"
#include "JSONOutput.h"
#include "FilteringEngine.h"
#include "SVFAnalyzer.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalValue.h"
#include <map>
#include <set>
#include <memory>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 符号分类和标识
//===----------------------------------------------------------------------===//

enum class SymbolScope {
    GLOBAL,        // 全局符号，外部可见
    STATIC,        // 静态符号，模块内可见
    EXTERNAL,      // 外部声明
    WEAK,          // 弱符号
    COMMON         // 通用符号
};

struct SymbolInfo {
    std::string name;
    std::string mangled_name;      // 完整的标识符
    std::string module_name;       // 所属模块
    SymbolScope scope;
    bool is_definition;            // true=定义，false=声明
    
    // 用于处理重名符号的唯一标识
    std::string getUniqueId() const {
        return module_name + "::" + name + "::" + std::to_string(static_cast<int>(scope));
    }
};

//===----------------------------------------------------------------------===//
// 增强的全局符号表
//===----------------------------------------------------------------------===//

struct EnhancedGlobalSymbolTable {
    // 函数符号表 - 按作用域分类
    std::map<std::string, std::vector<std::pair<Function*, SymbolInfo>>> functions_by_name;
    std::map<std::string, std::pair<Function*, SymbolInfo>> global_functions;
    std::map<std::string, std::vector<std::pair<Function*, SymbolInfo>>> static_functions;
    std::map<std::string, std::vector<Function*>> signature_to_functions;
    
    // 全局变量符号表 - 按作用域分类
    std::map<std::string, std::vector<std::pair<GlobalVariable*, SymbolInfo>>> variables_by_name;
    std::map<std::string, std::pair<GlobalVariable*, SymbolInfo>> global_variables;
    std::map<std::string, std::vector<std::pair<GlobalVariable*, SymbolInfo>>> static_variables;
    
    // 结构体类型表
    std::map<std::string, StructType*> struct_types;
    std::map<std::string, std::vector<StructType*>> struct_variants;
    
    // 模块映射
    std::map<Function*, Module*> function_to_module;
    std::map<GlobalVariable*, Module*> global_var_to_module;
    std::map<std::string, Module*> module_by_name;
    
    // 外部声明
    std::set<std::string> external_functions;
    std::set<std::string> external_globals;
    
    void clear() {
        functions_by_name.clear();
        global_functions.clear();
        static_functions.clear();
        signature_to_functions.clear();
        variables_by_name.clear();
        global_variables.clear();
        static_variables.clear();
        struct_types.clear();
        struct_variants.clear();
        function_to_module.clear();
        global_var_to_module.clear();
        module_by_name.clear();
        external_functions.clear();
        external_globals.clear();
    }
};

//===----------------------------------------------------------------------===//
// 数据流分析器
//===----------------------------------------------------------------------===//

struct DataFlowNode {
    Value* value;
    std::string node_type;  // "global", "static", "local", "parameter", "constant"
    std::string source_info; // 来源信息
    int confidence;         // 置信度
    Module* source_module;  // 源模块
    
    DataFlowNode() : value(nullptr), confidence(0), source_module(nullptr) {}
};

class DataFlowAnalyzer {
private:
    EnhancedGlobalSymbolTable* global_symbols;
    std::map<Value*, DataFlowNode> value_to_node_cache;
    
    /// 分析值的数据流来源
    DataFlowNode analyzeValueSource(Value* V, int depth = 0);
    
    /// 分析Load指令的数据流
    DataFlowNode analyzeLoadDataFlow(LoadInst* LI);
    
    /// 分析GEP指令的数据流
    DataFlowNode analyzeGEPDataFlow(GetElementPtrInst* GEP);
    
    /// 分析PHI节点的数据流
    DataFlowNode analyzePHIDataFlow(PHINode* PHI);
    
public:
    DataFlowAnalyzer(EnhancedGlobalSymbolTable* symbols) : global_symbols(symbols) {}
    
    /// 判断值是否为全局变量
    bool isGlobalVariable(Value* V);
    
    /// 判断值是否为静态变量
    bool isStaticVariable(Value* V);
    
    /// 获取值的完整数据流信息
    DataFlowNode getDataFlowInfo(Value* V);
    
    /// 清除缓存
    void clearCache() { value_to_node_cache.clear(); }
};

//===----------------------------------------------------------------------===//
// 函数指针深度分析器
//===----------------------------------------------------------------------===//

struct FunctionPointerCandidate {
    Function* function;
    int confidence;
    std::string match_reason;
    std::string module_source;
    SymbolScope scope;
    bool requires_further_analysis;
    
    FunctionPointerCandidate(Function* f, int conf, const std::string& reason, 
                           const std::string& module, SymbolScope s = SymbolScope::GLOBAL)
        : function(f), confidence(conf), match_reason(reason), 
          module_source(module), scope(s), requires_further_analysis(false) {}
};

class DeepFunctionPointerAnalyzer {
private:
    EnhancedGlobalSymbolTable* global_symbols;
    DataFlowAnalyzer* dataflow_analyzer;
    std::set<Function*> analyzed_functions;
    class CrossModuleAnalyzer* cross_module_analyzer;  // 前向声明引用
    
    /// 根据函数指针类型查找候选函数
    std::vector<FunctionPointerCandidate> findCandidatesByType(FunctionType* FT);
    
    /// 根据函数指针名称模式查找候选函数
    std::vector<FunctionPointerCandidate> findCandidatesByNamePattern(const std::string& pattern);
    
    /// 分析函数指针的存储位置
    std::vector<FunctionPointerCandidate> analyzeFunctionPointerStorage(Value* fp_value);
    
    /// 分析结构体中的函数指针字段
    std::vector<FunctionPointerCandidate> analyzeStructFunctionPointers(GetElementPtrInst* GEP);
    
    /// 分析全局函数指针表
    std::vector<FunctionPointerCandidate> analyzeGlobalFunctionTable(GlobalVariable* GV);
    
public:
    DeepFunctionPointerAnalyzer(EnhancedGlobalSymbolTable* symbols, DataFlowAnalyzer* dfa,
                               CrossModuleAnalyzer* cross_analyzer = nullptr)
        : global_symbols(symbols), dataflow_analyzer(dfa), cross_module_analyzer(cross_analyzer) {}
    
    /// 深度分析函数指针，返回所有可能的候选函数
    std::vector<FunctionPointerCandidate> analyzeDeep(Value* fp_value);
    
    /// 分析候选函数（递归分析）
    InterruptHandlerAnalysis analyzeCandidateFunction(Function* F);
    
    /// 清除分析缓存
    void clearCache() { analyzed_functions.clear(); }
    
private:
    /// 获取SVF分析器（如果可用）
    SVFEnhancedAnalyzer* getSVFAnalyzer();
};

//===----------------------------------------------------------------------===//
// 增强的跨模块分析器
//===----------------------------------------------------------------------===//

class CrossModuleAnalyzer {
private:
    // 模块存储
    std::vector<std::unique_ptr<Module>> modules;
    LLVMContext* context;
    
    // 增强的全局符号表
    EnhancedGlobalSymbolTable enhanced_symbols;
    
    // 数据布局
    std::unique_ptr<DataLayout> data_layout;
    
    // 专门的分析器
    std::unique_ptr<DataFlowAnalyzer> dataflow_analyzer;
    std::unique_ptr<DeepFunctionPointerAnalyzer> deep_fp_analyzer;
    std::unique_ptr<MemoryAccessAnalyzer> memory_analyzer;
    std::unique_ptr<InlineAsmAnalyzer> asm_analyzer;
    
    // SVF 增强分析器 (可选)
    std::unique_ptr<SVFEnhancedAnalyzer> svf_analyzer;
    bool enable_svf_analysis;
    
    // 过滤引擎
    std::unique_ptr<FilteringEngine> filtering_engine;
    
    /// 构建增强的全局符号表
    void buildEnhancedSymbolTable();
    
    /// 分析函数的链接性和作用域
    SymbolScope analyzeFunctionScope(Function* F);
    
    /// 分析全局变量的链接性和作用域
    SymbolScope analyzeGlobalVariableScope(GlobalVariable* GV);
    
    /// 获取函数签名字符串
    std::string getFunctionSignature(Function* F);
    
    /// 创建专门的分析器
    void createSpecializedAnalyzers();
    
public:
    CrossModuleAnalyzer() : context(nullptr), enable_svf_analysis(false) {}
    ~CrossModuleAnalyzer() = default;
    
    /// 加载所有模块
    bool loadAllModules(const std::vector<std::string>& bc_files, LLVMContext& Context);
    
    /// 设置SVF分析选项
    void enableSVFAnalysis(bool enable = true) { enable_svf_analysis = enable; }
    bool isSVFEnabled() const { return enable_svf_analysis; }
    SVFEnhancedAnalyzer* getSVFAnalyzer() { return svf_analyzer.get(); }
    
    /// 设置过滤配置
    void setFilteringConfig(const FilteringConfig& config);
    FilteringEngine* getFilteringEngine() { return filtering_engine.get(); }
    
    /// 进行增强的跨模块分析
    std::vector<InterruptHandlerAnalysis> analyzeAllHandlers(const std::string& handler_json);
    
    /// 分析单个处理函数（深度分析）
    InterruptHandlerAnalysis analyzeHandlerDeep(Function* F);
    
    /// 查找函数（考虑作用域）
    Function* findFunction(const std::string& name, const std::string& module_hint = "");
    
    /// 查找全局变量（考虑作用域）
    GlobalVariable* findGlobalVariable(const std::string& name, const std::string& module_hint = "");
    
    /// 分析处理函数的函数调用（包括深度函数指针分析）
    std::vector<FunctionCallInfo> analyzeHandlerFunctionCalls(Function* F);
    
    /// 根据签名查找函数
    std::vector<Function*> findFunctionsBySignature(const std::string& signature);
    
    /// 获取函数的作用域信息
    SymbolScope getFunctionScope(Function* F);
    
    /// 获取全局变量的作用域信息
    SymbolScope getGlobalVariableScope(GlobalVariable* GV);
    
    /// 获取结构体类型数量
    size_t getTotalStructTypes() const { return enhanced_symbols.struct_types.size(); }
    
    /// 获取统计信息
    size_t getTotalFunctions() const;
    size_t getTotalGlobalVars() const;
    size_t getTotalStaticFunctions() const;
    size_t getTotalStaticVars() const;
    size_t getModuleCount() const { return modules.size(); }
    
    /// 获取增强符号表的引用
    const EnhancedGlobalSymbolTable& getEnhancedSymbols() const { return enhanced_symbols; }
};

//===----------------------------------------------------------------------===//
// 增强的跨模块内存访问分析器
//===----------------------------------------------------------------------===//

class EnhancedCrossModuleMemoryAnalyzer : public MemoryAccessAnalyzer {
private:
    CrossModuleAnalyzer* cross_analyzer;
    DataFlowAnalyzer* dataflow_analyzer;
    
public:
    EnhancedCrossModuleMemoryAnalyzer(CrossModuleAnalyzer* analyzer, 
                                    DataFlowAnalyzer* dfa,
                                    const DataLayout* DL)
        : MemoryAccessAnalyzer(DL), cross_analyzer(analyzer), dataflow_analyzer(dfa) {}
    
    /// 增强的内存访问分析（考虑数据流）
    std::vector<MemoryAccessInfo> analyzeWithDataFlow(Function& F);
    
    /// 分析全局变量访问（区分static和global）
    MemoryAccessInfo analyzeGlobalVariableAccess(GlobalVariable* GV);
    
    /// 分析指针的数据流来源
    MemoryAccessInfo analyzePointerDataFlow(Value* ptr, bool is_write, Type* accessed_type);
};

#endif // IRQ_ANALYSIS_CROSS_MODULE_ANALYZER_H
