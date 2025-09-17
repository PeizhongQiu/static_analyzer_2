//===- InlineAsmAnalyzer.cpp - Inline Assembly Analyzer Implementation ===//

#include "InlineAsmAnalyzer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

std::vector<RegisterAccessInfo> InlineAsmAnalyzer::analyzeInlineAsm(const InlineAsm *IA) {
    std::vector<RegisterAccessInfo> reg_accesses;
    
    std::string asm_string = IA->getAsmString();
    std::string constraints = IA->getConstraintString();
    
    // 从汇编字符串中提取寄存器
    std::vector<std::string> used_registers = extractRegistersFromAsm(asm_string);
    
    // 判断是否为写操作
    bool is_write = isWriteConstraint(constraints);
    
    for (const std::string& reg : used_registers) {
        RegisterAccessInfo info;
        info.register_name = reg;
        info.is_write = is_write;
        info.inline_asm_constraint = constraints;
        reg_accesses.push_back(info);
    }
    
    // 如果没有从汇编字符串中提取到寄存器，基于约束进行分析
    if (reg_accesses.empty()) {
        analyzeConstraints(constraints, reg_accesses);
    }
    
    return reg_accesses;
}

bool InlineAsmAnalyzer::isWriteConstraint(const std::string& constraints) {
    // 检查约束字符串中的写操作标记
    return constraints.find("=") != std::string::npos ||  // 输出约束
           constraints.find("+") != std::string::npos;    // 输入输出约束
}

std::vector<std::string> InlineAsmAnalyzer::extractRegistersFromAsm(const std::string& asm_string) {
    std::vector<std::string> found_registers;
    
    // 搜索常用寄存器
    for (const std::string& reg : common_registers) {
        // 查找寄存器名称，确保是完整的单词边界
        size_t pos = 0;
        while ((pos = asm_string.find(reg, pos)) != std::string::npos) {
            // 检查前后字符，确保是完整的寄存器名
            bool is_complete_reg = true;
            
            if (pos > 0) {
                char prev_char = asm_string[pos - 1];
                if (std::isalnum(prev_char) || prev_char == '_' || prev_char == '%') {
                    is_complete_reg = false;
                }
            }
            
            if (pos + reg.length() < asm_string.length()) {
                char next_char = asm_string[pos + reg.length()];
                if (std::isalnum(next_char) || next_char == '_') {
                    is_complete_reg = false;
                }
            }
            
            if (is_complete_reg) {
                // 避免重复添加
                if (std::find(found_registers.begin(), found_registers.end(), reg) 
                    == found_registers.end()) {
                    found_registers.push_back(reg);
                }
            }
            
            pos += reg.length();
        }
    }
    
    return found_registers;
}

void InlineAsmAnalyzer::analyzeConstraints(const std::string& constraints, 
                                         std::vector<RegisterAccessInfo>& reg_accesses) {
    // 分析约束字符串来推断寄存器使用
    
    // 常见的约束到寄存器的映射
    struct ConstraintMapping {
        char constraint;
        std::string register_name;
        std::string description;
    };
    
    static const ConstraintMapping mappings[] = {
        {'a', "rax", "rax register constraint"},
        {'b', "rbx", "rbx register constraint"},
        {'c', "rcx", "rcx register constraint"},
        {'d', "rdx", "rdx register constraint"},
        {'S', "rsi", "rsi register constraint"},
        {'D', "rdi", "rdi register constraint"},
        {'r', "general", "general register constraint"},
        {'m', "memory", "memory constraint"},
        {'q', "abcd", "a,b,c,d register constraint"}
    };
    
    bool is_write = isWriteConstraint(constraints);
    
    for (const auto& mapping : mappings) {
        if (constraints.find(mapping.constraint) != std::string::npos) {
            RegisterAccessInfo info;
            info.register_name = mapping.register_name;
            info.is_write = is_write;
            info.inline_asm_constraint = constraints;
            info.source_location = mapping.description;
            reg_accesses.push_back(info);
        }
    }
    
    // 特殊处理一些常见的内联汇编模式
    if (constraints.find("cc") != std::string::npos) {
        // 条件码寄存器被修改
        RegisterAccessInfo info;
        info.register_name = "flags";
        info.is_write = true;
        info.inline_asm_constraint = constraints;
        info.source_location = "condition codes modified";
        reg_accesses.push_back(info);
    }
    
    if (constraints.find("memory") != std::string::npos) {
        // 内存屏障，可能影响所有寄存器
        RegisterAccessInfo info;
        info.register_name = "memory_barrier";
        info.is_write = true;
        info.inline_asm_constraint = constraints;
        info.source_location = "memory clobber";
        reg_accesses.push_back(info);
    }
}
