//===- SVFInterruptAnalyzer.cpp - SVF分析器核心实现 ----------------------===//

#include "SVFInterruptAnalyzer.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>

#ifdef SVF_AVAILABLE
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "SVF-LLVM/LLVMModule.h"
#include "WPA/Andersen.h"
#include "Graphs/VFG.h"
#include "Util/Options.h"
#include "Util/ExtAPI.h"
#endif

using namespace llvm;

//===----------------------------------------------------------------------===//
// 模块加载
//===----------------------------------------------------------------------===//

bool SVFInterruptAnalyzer::loadBitcodeFiles(const std::vector<std::string>& files) {
    outs() << "📦 Loading bitcode files...\n";
    outs() << "Total files to process: " << files.size() << "\n";
    
    if (!context) {
        errs() << "❌ No LLVM context provided\n";
        return false;
    }
    
    modules.clear();
    loaded_bc_files.clear();
    
    size_t loaded = 0;
    size_t failed = 0;
    
    for (const auto& file : files) {
        outs() << "Loading: " << file << "\n";
        
        auto BufferOrErr = MemoryBuffer::getFile(file);
        if (std::error_code EC = BufferOrErr.getError()) {
            outs() << "  ⚠️  Cannot read file: " << EC.message() << "\n";
            failed++;
            continue;
        }
        
        auto ModuleOrErr = parseBitcodeFile(BufferOrErr.get()->getMemBufferRef(), *context);
        if (!ModuleOrErr) {
            outs() << "  ⚠️  Cannot parse bitcode: " << toString(ModuleOrErr.takeError()) << "\n";
            failed++;
            continue;
        }
        
        auto M = std::move(ModuleOrErr.get());
        M->setModuleIdentifier(file);
        
        outs() << "  ✅ Loaded (" << M->size() << " functions)\n";
        
        modules.push_back(std::move(M));
        loaded_bc_files.push_back(file);
        loaded++;
    }
    
    outs() << "📊 Module loading summary:\n";
    outs() << "  ✅ Successfully loaded: " << loaded << "\n";
    outs() << "  ❌ Failed to load: " << failed << "\n";
    
    if (loaded == 0) {
        errs() << "❌ No modules loaded successfully\n";
        return false;
    }
    
    return true;
}

//===----------------------------------------------------------------------===//
// SVF初始化
//===----------------------------------------------------------------------===//

bool SVFInterruptAnalyzer::initializeSVF() {
    outs() << "🚀 Initializing SVF analysis framework...\n";
    
    if (loaded_bc_files.empty()) {
        errs() << "❌ No bitcode files loaded\n";
        return false;
    }
    
#ifdef SVF_AVAILABLE
    return initializeSVFCore();
#else
    errs() << "❌ SVF not available at compile time\n";
    return false;
#endif
}

#ifdef SVF_AVAILABLE
bool SVFInterruptAnalyzer::initializeSVFCore() {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    outs() << "🏗️  Building SVFIR (SVF Intermediate Representation)...\n";
    
    std::string extapi_path = "/home/qpz/lab/SVF/Release-build/lib/extapi.bc";
    outs() << "Setting extapi.bc path: " << extapi_path << "\n";
    
    SVF::ExtAPI::setExtBcPath(extapi_path);
    SVF::LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(loaded_bc_files);
    
    SVF::SVFIRBuilder builder;
    svfir = std::unique_ptr<SVF::SVFIR>(builder.build());
    
    if (!svfir) {
        errs() << "❌ Failed to build SVFIR\n";
        return false;
    }
    
    outs() << "✅ SVFIR built successfully\n";
    outs() << "📊 SVFIR Statistics:\n";
    outs() << "  Total nodes: " << svfir->getTotalNodeNum() << "\n";
    outs() << "  Total edges: " << svfir->getTotalEdgeNum() << "\n";
    outs() << "  Value nodes: " << svfir->getValueNodeNum() << "\n";
    
    if (!runPointerAnalysis()) {
        errs() << "❌ Pointer analysis failed\n";
        return false;
    }
    
    if (!buildVFG()) {
        outs() << "⚠️  VFG construction failed, but continuing with basic analysis\n";
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    outs() << "⏱️  SVF initialization completed in " << duration.count() << " ms\n";
    svf_initialized = true;
    return true;
}

bool SVFInterruptAnalyzer::runPointerAnalysis() {
    outs() << "🎯 Running Andersen pointer analysis...\n";
    
    pta = std::make_unique<SVF::AndersenWaveDiff>(svfir.get());
    pta->analyze();
    
    outs() << "✅ Pointer analysis completed\n";
    return true;
}

bool SVFInterruptAnalyzer::buildVFG() {
    outs() << "🌐 Building Value Flow Graph...\n";
    
    try {
        vfg = std::make_unique<SVF::VFG>(pta->getCallGraph());
        outs() << "✅ VFG built successfully\n";
        return true;
    } catch (const std::exception& e) {
        outs() << "⚠️  VFG construction failed: " << e.what() << "\n";
        return false;
    }
}
#endif

//===----------------------------------------------------------------------===//
// 函数查找
//===----------------------------------------------------------------------===//

Function* SVFInterruptAnalyzer::findFunction(const std::string& name) {
    for (auto& M : modules) {
        for (auto& F : *M) {
            if (F.getName() == name) {
                return &F;
            }
        }
    }
    return nullptr;
}

//===----------------------------------------------------------------------===//
// 重新设计的分析流程
//===----------------------------------------------------------------------===//

std::vector<InterruptHandlerResult> SVFInterruptAnalyzer::analyzeInterruptHandlers(const std::vector<std::string>& handler_names) {
    std::vector<InterruptHandlerResult> results;
    
    if (!svf_initialized) {
        errs() << "❌ SVF not initialized\n";
        return results;
    }
    
    outs() << "🔍 Starting enhanced interrupt handler analysis...\n";
    outs() << "📊 Handlers to analyze: " << handler_names.size() << "\n";
    outs() << std::string(60, '=') << "\n";
    
    for (const auto& name : handler_names) {
        outs() << "🎯 Analyzing handler: " << name << "\n";
        
        Function* handler = findFunction(name);
        if (!handler) {
            outs() << "  ❌ Function not found in loaded modules\n";
            
            InterruptHandlerResult result;
            result.function_name = name;
            result.analysis_complete = false;
            results.push_back(result);
            continue;
        }
        
        outs() << "  ✅ Function found in module: " << handler->getParent()->getName() << "\n";
        
        InterruptHandlerResult result = analyzeSingleHandlerComplete(handler);
        results.push_back(result);
        
        printAnalysisSummary(result);
    }
    
    outs() << "✅ All handlers analyzed with complete call graph analysis\n";
    return results;
}

InterruptHandlerResult SVFInterruptAnalyzer::analyzeSingleHandlerComplete(Function* handler) {
    InterruptHandlerResult result;
    result.function_name = handler->getName().str();
    result.module_file = handler->getParent()->getName().str();
    
    // 获取源文件信息
    if (auto* SP = handler->getSubprogram()) {
        result.source_file = SP->getFilename().str();
    }
    
    // 基础统计
    result.total_basic_blocks = handler->size();
    for (auto& BB : *handler) {
        result.total_instructions += BB.size();
    }
    
    // 第一阶段：构建完整的函数调用图和函数指针解析
    outs() << "  📋 Phase 1: Building complete call graph...\n";
    CallGraphInfo call_graph = buildCompleteCallGraph(handler);
    
    outs() << "    📊 Call graph statistics:\n";
    outs() << "      Direct functions: " << call_graph.direct_functions.size() << "\n";
    outs() << "      Indirect functions: " << call_graph.indirect_functions.size() << "\n";
    outs() << "      Function pointers: " << call_graph.function_pointers.size() << "\n";
    outs() << "      Total unique functions: " << call_graph.all_functions.size() << "\n";
    
    // 第二阶段：基于完整函数列表进行各种分析
    outs() << "  📋 Phase 2: Analyzing memory operations across all functions...\n";
    analyzeMemoryOperationsComplete(call_graph, result);
    
    outs() << "  📋 Phase 3: Analyzing global/static variable modifications...\n";
    analyzeGlobalAndStaticWritesComplete(call_graph, result);
    
    outs() << "  📋 Phase 4: Analyzing data structure accesses...\n";
    analyzeDataStructuresComplete(call_graph, result);
    
    outs() << "  📋 Phase 5: Finalizing results...\n";
    finalizeAnalysisResults(call_graph, result);
    
    // 计算置信度
    result.confidence_score = calculateConfidence(result);
    result.analysis_complete = true;
    
    return result;
}

//===----------------------------------------------------------------------===//
// 完整调用图构建
//===----------------------------------------------------------------------===//

CallGraphInfo SVFInterruptAnalyzer::buildCompleteCallGraph(Function* root_function) {
    CallGraphInfo call_graph;
    std::set<Function*> visited_functions;
    
    outs() << "    🔄 Building call graph starting from: " << root_function->getName().str() << "\n";
    
    // 递归构建调用图
    buildCallGraphRecursive(root_function, call_graph, visited_functions, 0);
    
    // 收集所有唯一函数
    call_graph.all_functions.insert(call_graph.direct_functions.begin(), call_graph.direct_functions.end());
    call_graph.all_functions.insert(call_graph.indirect_functions.begin(), call_graph.indirect_functions.end());
    
    return call_graph;
}

void SVFInterruptAnalyzer::buildCallGraphRecursive(Function* function, 
                                                  CallGraphInfo& call_graph, 
                                                  std::set<Function*>& visited_functions, 
                                                  int depth) {
    // 防止无限递归和重复分析
    if (depth > 15 || visited_functions.find(function) != visited_functions.end()) {
        return;
    }
    
    visited_functions.insert(function);
    outs() << std::string(depth * 2, ' ') << "🔍 Analyzing calls in: " << function->getName().str() << "\n";
    
    for (auto& BB : *function) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (CI->getCalledFunction()) {
                    // 直接函数调用
                    Function* callee = CI->getCalledFunction();
                    std::string func_name = callee->getName().str();
                    
                    // 跳过内部函数
                    if (isInternalFunction(func_name)) {
                        continue;
                    }
                    
                    outs() << std::string((depth + 1) * 2, ' ') << "📞 Direct call: " << func_name << "\n";
                    
                    call_graph.direct_functions.insert(callee);
                    call_graph.call_sites[callee].push_back(getInstructionLocation(&I));
                    
                    // 递归分析被调用的函数
                    buildCallGraphRecursive(callee, call_graph, visited_functions, depth + 1);
                    
                } else {
                    // 间接函数调用
                    Value* called_value = CI->getCalledOperand();
                    std::vector<std::string> targets = resolveFunctionPointer(called_value);
                    
                    std::string call_site = getInstructionLocation(&I);
                    call_graph.indirect_call_sites.push_back(call_site);
                    
                    for (const auto& target_name : targets) {
                        outs() << std::string((depth + 1) * 2, ' ') << "🎯 Indirect target: " << target_name << "\n";
                        
                        // 记录函数指针信息
                        call_graph.function_pointers[call_site].push_back(target_name);
                        
                        // 查找目标函数并递归分析
                        Function* target_func = findFunction(target_name);
                        if (target_func) {
                            call_graph.indirect_functions.insert(target_func);
                            call_graph.call_sites[target_func].push_back(call_site);
                            buildCallGraphRecursive(target_func, call_graph, visited_functions, depth + 1);
                        }
                    }
                }
            }
        }
    }
}

//===----------------------------------------------------------------------===//
// 完整的内存操作分析
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::analyzeMemoryOperationsComplete(const CallGraphInfo& call_graph, InterruptHandlerResult& result) {
    outs() << "    🔄 Analyzing memory operations in " << call_graph.all_functions.size() << " functions...\n";
    
    for (Function* func : call_graph.all_functions) {
        outs() << "      📝 Memory analysis: " << func->getName().str() << "\n";
        analyzeMemoryOperationsInFunction(func, result);
    }
    
    // 合并和排序写操作
    consolidateWriteOperations(result);
    
    outs() << "    ✅ Memory analysis completed:\n";
    outs() << "      Read operations: " << result.memory_read_operations << "\n";
    outs() << "      Write operations: " << result.memory_write_operations << "\n";
    outs() << "      Unique write targets: " << result.memory_writes.size() << "\n";
}

void SVFInterruptAnalyzer::analyzeGlobalAndStaticWritesComplete(const CallGraphInfo& call_graph, InterruptHandlerResult& result) {
    outs() << "    🔄 Analyzing global/static writes in " << call_graph.all_functions.size() << " functions...\n";
    
    std::set<std::string> all_modified_globals;
    std::set<std::string> all_modified_statics;
    
    for (Function* func : call_graph.all_functions) {
        outs() << "      🌐 Global analysis: " << func->getName().str() << "\n";
        analyzeGlobalWritesInFunction(func, all_modified_globals, all_modified_statics);
    }
    
    // 转换为vector
    result.modified_global_vars.assign(all_modified_globals.begin(), all_modified_globals.end());
    result.modified_static_vars.assign(all_modified_statics.begin(), all_modified_statics.end());
    
    outs() << "    ✅ Global analysis completed:\n";
    outs() << "      Modified global vars: " << result.modified_global_vars.size() << "\n";
    outs() << "      Modified static vars: " << result.modified_static_vars.size() << "\n";
}

void SVFInterruptAnalyzer::analyzeDataStructuresComplete(const CallGraphInfo& call_graph, InterruptHandlerResult& result) {
    outs() << "    🔄 Analyzing data structures in " << call_graph.all_functions.size() << " functions...\n";
    
    std::map<std::string, DataStructureAccess> unique_accesses;
    
    for (Function* func : call_graph.all_functions) {
        outs() << "      🏗️  Data structure analysis: " << func->getName().str() << "\n";
        analyzeDataStructuresInFunction(func, unique_accesses);
    }
    
    // 转换为vector并排序
    for (const auto& pair : unique_accesses) {
        result.data_structure_accesses.push_back(pair.second);
    }
    
    std::sort(result.data_structure_accesses.begin(), result.data_structure_accesses.end(),
              [](const DataStructureAccess& a, const DataStructureAccess& b) {
                  if (a.struct_name != b.struct_name) {
                      return a.struct_name < b.struct_name;
                  }
                  return a.field_name < b.field_name;
              });
    
    outs() << "    ✅ Data structure analysis completed:\n";
    outs() << "      Unique structure accesses: " << result.data_structure_accesses.size() << "\n";
}

void SVFInterruptAnalyzer::finalizeAnalysisResults(const CallGraphInfo& call_graph, InterruptHandlerResult& result) {
    // 填充函数调用详情
    result.function_calls = 0;
    result.indirect_calls = call_graph.indirect_call_sites.size();
    
    // 直接函数调用
    for (Function* func : call_graph.direct_functions) {
        result.direct_function_calls.push_back(func->getName().str());
        result.function_calls++;
    }
    
    // 间接函数调用目标
    for (const auto& pair : call_graph.function_pointers) {
        for (const auto& target : pair.second) {
            result.indirect_call_targets.push_back(target);
        }
    }
    
    // 函数指针目标
    result.function_pointer_targets = call_graph.function_pointers;
    
    // 检测中断处理特征
    for (Function* func : call_graph.all_functions) {
        std::string func_name = func->getName().str();
        if (isDeviceRelatedFunction(func_name)) {
            result.has_device_access = true;
        }
        if (isInterruptRelatedFunction(func_name)) {
            result.has_irq_operations = true;
        }
        if (isWorkQueueFunction(func_name)) {
            result.has_work_queue_ops = true;
        }
    }
}

//===----------------------------------------------------------------------===//
// 工具函数
//===----------------------------------------------------------------------===//

std::string SVFInterruptAnalyzer::getInstructionLocation(const Instruction* inst) {
    std::string location = "unknown";
    
    if (const DebugLoc &DL = inst->getDebugLoc()) {
        location = DL->getFilename().str() + ":" + std::to_string(DL.getLine());
    }
    
    return location;
}

bool SVFInterruptAnalyzer::isInterruptRelatedFunction(const std::string& name) {
    static const std::vector<std::string> keywords = {
        "irq", "interrupt", "disable", "enable", "mask", "unmask", 
        "ack", "eoi", "handler", "isr", "softirq"
    };
    
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    for (const auto& keyword : keywords) {
        if (lower_name.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool SVFInterruptAnalyzer::isDeviceRelatedFunction(const std::string& name) {
    static const std::vector<std::string> keywords = {
        "pci", "device", "dev", "read", "write", "reg", "mmio", 
        "ioread", "iowrite", "inb", "outb", "readl", "writel"
    };
    
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    for (const auto& keyword : keywords) {
        if (lower_name.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool SVFInterruptAnalyzer::isInternalFunction(const std::string& name) {
    // 跳过LLVM内部函数和调试函数
    static const std::vector<std::string> internal_prefixes = {
        "llvm.", "__llvm", "__sanitizer", "__asan", "__msan", "__tsan",
        "__builtin", "__stack_chk", "___stack_chk"
    };
    
    for (const auto& prefix : internal_prefixes) {
        if (name.find(prefix) == 0) {
            return true;
        }
    }
    
    return false;
}

void SVFInterruptAnalyzer::printAnalysisSummary(const InterruptHandlerResult& result) {
    outs() << "  📊 Complete Analysis Summary:\n";
    outs() << "    Instructions: " << result.total_instructions << "\n";
    outs() << "    Basic blocks: " << result.total_basic_blocks << "\n";
    outs() << "    Memory reads: " << result.memory_read_operations << "\n";
    outs() << "    Memory writes: " << result.memory_write_operations << "\n";
    outs() << "    Direct function calls: " << result.direct_function_calls.size() << "\n";
    outs() << "    Indirect calls: " << result.indirect_calls << "\n";
    outs() << "    Data structure accesses: " << result.data_structure_accesses.size() << "\n";
    outs() << "    Modified global vars: " << result.modified_global_vars.size() << "\n";
    outs() << "    Modified static vars: " << result.modified_static_vars.size() << "\n";
    outs() << "    Function pointer targets: " << result.function_pointer_targets.size() << "\n";
    outs() << "    Confidence: " << result.confidence_score << "/100\n";
    outs() << std::string(40, '-') << "\n";
}

#ifdef SVF_AVAILABLE
const SVF::Function* SVFInterruptAnalyzer::findSVFFunction(const std::string& name) {
    return nullptr;  // 简化实现
}
#endif
