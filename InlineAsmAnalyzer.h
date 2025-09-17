//===- InlineAsmAnalyzer.h - Inline Assembly Analyzer -------------------===//
//
// 分析内联汇编中的寄存器访问
//
//===----------------------------------------------------------------------===//

#ifndef IRQ_ANALYSIS_INLINE_ASM_ANALYZER_H
#define IRQ_ANALYSIS_INLINE_ASM_ANALYZER_H

#include "DataStructures.h"
#include "llvm/IR/InlineAsm.h"
#include <vector>

using namespace llvm;

//===----------------------------------------------------------------------===//
// 内联汇编分析器
//===----------------------------------------------------------------------===//

class InlineAsmAnalyzer {
private:
    /// 常用寄存器列表
    std::vector<std::string> common_registers = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "eax", "ebx", "ecx", "edx", "esi", "edi",
        "ax", "bx", "cx", "dx", "al", "bl", "cl", "dl"
    };
    
    /// 解析约束字符串确定寄存器访问模式
    bool isWriteConstraint(const std::string& constraints);
    
    /// 从汇编字符串中提取寄存器使用
    std::vector<std::string> extractRegistersFromAsm(const std::string& asm_string);
    
    /// 分析约束字符串
    void analyzeConstraints(const std::string& constraints, 
                           std::vector<RegisterAccessInfo>& reg_accesses);
    
public:
    /// 分析内联汇编指令
    std::vector<RegisterAccessInfo> analyzeInlineAsm(const InlineAsm *IA);
};

#endif // IRQ_ANALYSIS_INLINE_ASM_ANALYZER_H
