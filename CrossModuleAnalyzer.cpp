//===- CrossModuleAnalyzer.cpp - 跨模块分析器实现 ------------------------===//

#include "CrossModuleAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// CrossModuleAnalyzer 主要实现
//===----------------------------------------------------------------------===//

bool CrossModuleAnalyzer::loadAllModules(const std::vector<std::string>& bc_files, LLVMContext& Context) {
    context = &Context;
    modules.clear();
    enhanced_symbols.clear();
    
    outs() << "Loading modules for cross-module analysis...\n";
    
    size_t loaded = 0;
    size_t total = bc_files.size();
    
    for (const auto& bc_file : bc_files) {
        outs() << "Loading: " << bc_file << "... ";
        
        ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr = 
            MemoryBuffer::getFile(bc_file);
        
        if (std::error_code EC = BufferOrErr.getError()) {
            outs() << "✗ Failed to read file: " << EC.message() << "\n";
            continue;
        }
        
        Expected<std::unique_ptr<Module>> ModuleOrErr = 
            parseBitcodeFile(BufferOrErr.get()->getMemBufferRef(), Context);
        
        if (!ModuleOrErr) {
            outs() << "✗ Failed to parse bitcode: " << toString(ModuleOrErr.takeError()) << "\n";
            continue;
        }
        
        std::unique_ptr<Module> M = std::move(ModuleOrErr.get());
        if (!M) {
            outs() << "✗ Null module\n";
            continue;
        }
        
        // 设置模块名称为文件名
        M->setModuleIdentifier(bc_file);
        
        outs() << "✓ (" << M->size() << " functions, " 
               << M->getGlobalList().size() << " globals)\n";
        
        modules.push_back(std::move(M));
        loaded++;
    }
    
    outs() << "\nModule loading summary: " << loaded << "/" << total << " modules loaded\n";
    
    if (loaded == 0) {
        errs() << "Error: No modules loaded successfully\n";
        return false;
    }
    
    // 构建增强符号表
    buildEnhancedSymbolTable();
    
    // 创建专门的分析器
    createSpecializedAnalyzers();
    
    outs() << "Cross-module analysis setup completed\n\n";
    return true;
}

void CrossModuleAnalyzer::buildEnhancedSymbolTable() {
    outs() << "Building enhanced symbol table...\n";
    
    size_t global_funcs = 0, static_funcs = 0;
    size_t global_vars = 0, static_vars = 0;
    size_t structs = 0;
    
    // 遍历所有模块
    for (auto& M : modules) {
        Module* mod = M.get();
        enhanced_symbols.module_by_name[mod->getName().str()] = mod;
        
        // 分析函数
        for (auto& F : *mod) {
            if (F.isDeclaration()) {
                enhanced_symbols.external_functions.insert(F.getName().str());
                continue;
            }
            
            enhanced_symbols.function_to_module[&F] = mod;
            
            SymbolScope scope = analyzeFunctionScope(&F);
            SymbolInfo info;
            info.name = F.getName().str();
            info.mangled_name = F.getName().str();
            info.module_name = mod->getName().str();
            info.scope = scope;
            info.is_definition = true;
            
            // 添加到适当的符号表
            enhanced_symbols.functions_by_name[info.name].push_back(std::make_pair(&F, info));
            
            if (scope == SymbolScope::GLOBAL) {
                enhanced_symbols.global_functions[info.name] = std::make_pair(&F, info);
                global_funcs++;
            } else {
                enhanced_symbols.static_functions[mod->getName().str()].push_back(std::make_pair(&F, info));
                static_funcs++;
            }
            
            // 添加到函数签名映射
            std::string signature = getFunctionSignature(&F);
            enhanced_symbols.signature_to_functions[signature].push_back(&F);
        }
        
        // 分析全局变量
        for (auto& GV : mod->getGlobalList()) {
            enhanced_symbols.global_var_to_module[&GV] = mod;
            
            SymbolScope scope = analyzeGlobalVariableScope(&GV);
            SymbolInfo info;
            info.name = GV.getName().str();
            info.mangled_name = GV.getName().str();
            info.module_name = mod->getName().str();
            info.scope = scope;
            info.is_definition = !GV.isDeclaration();
            
            enhanced_symbols.variables_by_name[info.name].push_back(std::make_pair(&GV, info));
            
            if (scope == SymbolScope::GLOBAL) {
                enhanced_symbols.global_variables[info.name] = std::make_pair(&GV, info);
                global_vars++;
            } else {
                enhanced_symbols.static_variables[mod->getName().str()].push_back(std::make_pair(&GV, info));
                static_vars++;
            }
        }
        
        // 收集结构体类型
        for (auto& ST : mod->getIdentifiedStructTypes()) {
            if (ST->hasName()) {
                std::string name = ST->getName().str();
                enhanced_symbols.struct_types[name] = ST;
                enhanced_symbols.struct_variants[name].push_back(ST);
                structs++;
            }
        }
    }
    
    outs() << "Symbol table built:\n";
    outs() << "  Global functions: " << global_funcs << "\n";
    outs() << "  Static functions: " << static_funcs << "\n";
    outs() << "  Global variables: " << global_vars << "\n";
    outs() << "  Static variables: " << static_vars << "\n";
    outs() << "  Structure types: " << structs << "\n";
}

void CrossModuleAnalyzer::createSpecializedAnalyzers() {
    // 设置数据布局（使用第一个模块的）
    if (!modules.empty()) {
        data_layout = std::make_unique<DataLayout>(modules[0]->getDataLayout());
    }
    
    // 创建专门的分析器
    dataflow_analyzer = std::make_unique<DataFlowAnalyzer>(&enhanced_symbols);
    deep_fp_analyzer = std::make_unique<DeepFunctionPointerAnalyzer>(&enhanced_symbols, dataflow_analyzer.get());
    memory_analyzer = std::make_unique<EnhancedCrossModuleMemoryAnalyzer>(this, dataflow_analyzer.get(), data_layout.get());
    asm_analyzer = std::make_unique<InlineAsmAnalyzer>();
}

SymbolScope CrossModuleAnalyzer::analyzeFunctionScope(Function* F) {
    if (!F) return SymbolScope::STATIC;
    
    GlobalValue::LinkageTypes linkage = F->getLinkage();
    
    switch (linkage) {
        case GlobalValue::ExternalLinkage:
        case GlobalValue::ExternalWeakLinkage:
            return SymbolScope::GLOBAL;
        case GlobalValue::InternalLinkage:
        case GlobalValue::PrivateLinkage:
            return SymbolScope::STATIC;
        case GlobalValue::WeakAnyLinkage:
        case GlobalValue::WeakODRLinkage:
            return SymbolScope::WEAK;
        case GlobalValue::CommonLinkage:
            return SymbolScope::COMMON;
        default:
            return SymbolScope::STATIC;
    }
}

SymbolScope CrossModuleAnalyzer::analyzeGlobalVariableScope(GlobalVariable* GV) {
    if (!GV) return SymbolScope::STATIC;
    
    GlobalValue::LinkageTypes linkage = GV->getLinkage();
    
    switch (linkage) {
        case GlobalValue::ExternalLinkage:
        case GlobalValue::ExternalWeakLinkage:
            return SymbolScope::GLOBAL;
        case GlobalValue::InternalLinkage:
        case GlobalValue::PrivateLinkage:
            return SymbolScope::STATIC;
        case GlobalValue::WeakAnyLinkage:
        case GlobalValue::WeakODRLinkage:
            return SymbolScope::WEAK;
        case GlobalValue::CommonLinkage:
            return SymbolScope::COMMON;
        default:
            return SymbolScope::STATIC;
    }
}

std::string CrossModuleAnalyzer::getFunctionSignature(Function* F) {
    if (!F) return "";
    
    FunctionType* FT = F->getFunctionType();
    std::string signature;
    signature += std::to_string(FT->getReturnType()->getTypeID()) + "_";
    
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
        signature += std::to_string(FT->getParamType(i)->getTypeID()) + "_";
    }
    
    return signature;
}

Function* CrossModuleAnalyzer::findFunction(const std::string& name, const std::string& module_hint) {
    // 首先尝试全局函数
    auto global_it = enhanced_symbols.global_functions.find(name);
    if (global_it != enhanced_symbols.global_functions.end()) {
        return global_it->second.first;
    }
    
    // 如果有模块提示，在该模块中查找静态函数
    if (!module_hint.empty()) {
        auto static_it = enhanced_symbols.static_functions.find(module_hint);
        if (static_it != enhanced_symbols.static_functions.end()) {
            for (const auto& func_pair : static_it->second) {
                if (func_pair.second.name == name) {
                    return func_pair.first;
                }
            }
        }
    }
    
    // 在所有函数中查找
    auto all_it = enhanced_symbols.functions_by_name.find(name);
    if (all_it != enhanced_symbols.functions_by_name.end() && !all_it->second.empty()) {
        return all_it->second[0].first;
    }
    
    return nullptr;
}

GlobalVariable* CrossModuleAnalyzer::findGlobalVariable(const std::string& name, const std::string& module_hint) {
    // 首先尝试全局变量
    auto global_it = enhanced_symbols.global_variables.find(name);
    if (global_it != enhanced_symbols.global_variables.end()) {
        return global_it->second.first;
    }
    
    // 如果有模块提示，在该模块中查找静态变量
    if (!module_hint.empty()) {
        auto static_it = enhanced_symbols.static_variables.find(module_hint);
        if (static_it != enhanced_symbols.static_variables.end()) {
            for (const auto& var_pair : static_it->second) {
                if (var_pair.second.name == name) {
                    return var_pair.first;
                }
            }
        }
    }
    
    // 在所有变量中查找
    auto all_it = enhanced_symbols.variables_by_name.find(name);
    if (all_it != enhanced_symbols.variables_by_name.end() && !all_it->second.empty()) {
        return all_it->second[0].first;
    }
    
    return nullptr;
}

std::vector<Function*> CrossModuleAnalyzer::findFunctionsBySignature(const std::string& signature) {
    auto it = enhanced_symbols.signature_to_functions.find(signature);
    if (it != enhanced_symbols.signature_to_functions.end()) {
        return it->second;
    }
    return {};
}

SymbolScope CrossModuleAnalyzer::getFunctionScope(Function* F) {
    if (!F) return SymbolScope::STATIC;
    return analyzeFunctionScope(F);
}

SymbolScope CrossModuleAnalyzer::getGlobalVariableScope(GlobalVariable* GV) {
    if (!GV) return SymbolScope::STATIC;
    return analyzeGlobalVariableScope(GV);
}

size_t CrossModuleAnalyzer::getTotalFunctions() const {
    return enhanced_symbols.global_functions.size();
}

size_t CrossModuleAnalyzer::getTotalGlobalVars() const {
    return enhanced_symbols.global_variables.size();
}

size_t CrossModuleAnalyzer::getTotalStaticFunctions() const {
    size_t total = 0;
    for (const auto& module_pair : enhanced_symbols.static_functions) {
        total += module_pair.second.size();
    }
    return total;
}

size_t CrossModuleAnalyzer::getTotalStaticVars() const {
    size_t total = 0;
    for (const auto& module_pair : enhanced_symbols.static_variables) {
        total += module_pair.second.size();
    }
    return total;
}
