//===- MemoryAccessAnalyzer.h - Memory Access Analyzer ------------------===//
//
// 分析函数中的内存访问模式，包括指针链追踪
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_MEMORY_ACCESS_ANALYZER_H
#define IRQ_ANALYSIS_MEMORY_ACCESS_ANALYZER_H

#include "DataStructures.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/DataLayout.h"
#include <map>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 内存访问分析器
//===----------------------------------------------------------------------===//

class MemoryAccessAnalyzer {
private:
    const DataLayout *DL;
    std::map<Value*, PointerChain> pointer_chain_cache; // 缓存已分析的指针链
    
    // 追踪指针链的最大深度，防止无限递归
    static const int MAX_CHAIN_DEPTH = 10;
    
    /// 检查函数是否是中断处理函数
    bool isIRQHandlerFunction(Function &F);
    
    /// 追踪指针链
    PointerChain tracePointerChain(Value *ptr, int depth = 0);
    
    /// 分析GEP指令
    MemoryAccessInfo analyzeGEPInstruction(GetElementPtrInst *GEP);
    
    /// 分析全局变量访问
    MemoryAccessInfo analyzeGlobalVariable(GlobalVariable *GV);
    
    /// 结合指针链信息分析Load/Store指令
    MemoryAccessInfo analyzeLoadStoreWithChain(Value *ptr, bool is_write, Type *accessed_type, bool is_irq_handler);
    
    /// 简单的Load/Store分析（后备方法）
    MemoryAccessInfo analyzeLoadStore(Value *ptr, bool is_write);
    
public:
    MemoryAccessAnalyzer(const DataLayout *DL) : DL(DL) {}
    
    /// 分析函数中的所有内存访问
    std::vector<MemoryAccessInfo> analyzeFunction(Function &F);
    
    /// 清空指针链缓存
    void clearCache() { pointer_chain_cache.clear(); }
};

#endif // IRQ_ANALYSIS_MEMORY_ACCESS_ANALYZER_H
