//===- FunctionPointerAnalyzer.h - Function Pointer Analyzer ------------===//
//
// 分析函数指针的可能目标函数
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_FUNCTION_POINTER_ANALYZER_H
#define IRQ_ANALYSIS_FUNCTION_POINTER_ANALYZER_H

#include "DataStructures.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#include <map>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 函数指针分析器
//===----------------------------------------------------------------------===//

class FunctionPointerAnalyzer {
private:
    Module *M;
    const DataLayout *DL;
    
    // 缓存分析结果
    std::map<Value*, FunctionPointerAnalysis> fp_analysis_cache;
    std::map<Function*, std::vector<MemoryAccessInfo>> function_memory_cache;
    
    // 函数签名匹配
    std::map<std::string, std::vector<Function*>> signature_to_functions;
    
    /// 构建函数签名到函数的映射
    void buildFunctionSignatureMap();
    
    /// 获取函数签名字符串
    std::string getFunctionSignature(Function *F);
    
    /// 获取Value的名称
    std::string getValueName(Value *V);
    
    /// 分析常量表达式中的函数指针
    void analyzeConstantExpr(ConstantExpr *CE, FunctionPointerAnalysis &analysis);
    
    /// 分析全局变量中的函数指针
    void analyzeGlobalVariableFP(GlobalVariable *GV, FunctionPointerAnalysis &analysis);
    
    /// 分析从内存加载的函数指针
    void analyzeLoadedFunctionPointer(LoadInst *LI, FunctionPointerAnalysis &analysis);
    
    /// 分析结构体字段中的函数指针
    void analyzeStructFieldFunctionPointer(GetElementPtrInst *GEP, FunctionPointerAnalysis &analysis);
    
    /// 分析PHI节点中的函数指针
    void analyzePHINodeFunctionPointer(PHINode *PHI, FunctionPointerAnalysis &analysis);
    
    /// 分析函数参数传入的函数指针
    void analyzeArgumentFunctionPointer(Argument *Arg, FunctionPointerAnalysis &analysis);
    
    /// 基于类型的启发式分析
    void performHeuristicAnalysis(Value *fp_value, FunctionPointerAnalysis &analysis);
    
    /// 分析对指针位置的存储操作
    void analyzeStoresTo(Value *ptr, FunctionPointerAnalysis &analysis);
    
    /// 查找结构体字段中的函数指针赋值
    void findFunctionPointersInStructField(const std::string &struct_name, int field_index, 
                                         FunctionPointerAnalysis &analysis);
    
    /// 检查GEP是否匹配指定的结构体字段
    bool matchesStructField(GetElementPtrInst *GEP, const std::string &struct_name, int field_index);
    
    /// 获取函数类型签名
    std::string getFunctionTypeSignature(FunctionType *FT);
    
    /// 检查是否是可能的回调函数
    bool isLikelyCallbackFunction(Function *F);
    
    /// 去除重复的目标函数
    void removeDuplicateTargets(FunctionPointerAnalysis &analysis);
    
    /// 获取函数的内存访问信息
    std::vector<MemoryAccessInfo> getFunctionMemoryAccesses(Function *F);
    
    /// 获取函数的寄存器访问信息
    std::vector<RegisterAccessInfo> getFunctionRegisterAccesses(Function *F);
    
public:
    FunctionPointerAnalyzer(Module *M, const DataLayout *DL);
    
    /// 分析函数指针的可能目标
    FunctionPointerAnalysis analyzeFunctionPointer(Value *fp_value);
    
    /// 聚合间接调用的所有可能影响
    IndirectCallAnalysis analyzeIndirectCall(CallInst *CI);
    
    /// 清空分析缓存
    void clearCache() { 
        fp_analysis_cache.clear(); 
        function_memory_cache.clear();
    }
};

#endif // IRQ_ANALYSIS_FUNCTION_POINTER_ANALYZER_H
