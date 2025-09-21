#include <fstream>
#ifdef SVF_AVAILABLE
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "SVF-LLVM/LLVMModule.h"
#include "SVFIR/SVFIR.h"
#include "WPA/Andersen.h"
#include "Graphs/VFG.h"
#include "Util/Options.h"
#include "Util/ExtAPI.h"
#endif

//===- SVFInterruptAnalyzer.cpp - SVF中断处理函数分析器实现 --------------===//

#include "SVFInterruptAnalyzer.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/JSON.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include <fstream>
#include <chrono>
#include <algorithm>

#ifdef SVF_AVAILABLE
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "SVF-LLVM/LLVMModule.h"
#include "WPA/Andersen.h"
#include "Graphs/VFG.h"
#include "Util/Options.h"
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
    outs() << "Input files for SVF:\n";
    for (size_t i = 0; i < loaded_bc_files.size() && i < 10; ++i) {
        outs() << "  [" << (i+1) << "] " << loaded_bc_files[i] << "\n";
    }
    if (loaded_bc_files.size() > 10) {
        outs() << "  ... and " << (loaded_bc_files.size() - 10) << " more files\n";
    }
    
    // 使用ExtAPI::setExtBcPath()设置extapi.bc路径
    std::string extapi_path = "/home/qpz/lab/SVF/Release-build/lib/extapi.bc";
    outs() << "Setting extapi.bc path: " << extapi_path << "\n";
    
    // 在buildSVFModule之前调用setExtBcPath
    SVF::ExtAPI::setExtBcPath(extapi_path);
    
    // 使用正确的buildSVFModule API
    SVF::LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(loaded_bc_files);
    
    // 1. 构建SVFIR - 直接使用SVFIRBuilder
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
    
    // 使用正确的API获取对象节点数量
    // 通过LLVMModuleSet获取
    SVF::LLVMModuleSet* moduleSet = SVF::LLVMModuleSet::getLLVMModuleSet();
    outs() << "  Object nodes: " << moduleSet->getObjNodeNum() << "\n";
    
    if (svfir->getTotalNodeNum() == 0) {
        errs() << "⚠️  SVFIR has no nodes - this indicates a problem with module loading\n";
        return false;
    }
    
    // 2. 运行指针分析
    if (!runPointerAnalysis()) {
        errs() << "❌ Pointer analysis failed\n";
        return false;
    }
    
    // 3. 构建VFG（可选）
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
    outs() << "📊 Pointer Analysis Statistics:\n";
    outs() << "  Analysis completed successfully\n";
    
    return true;
}

bool SVFInterruptAnalyzer::buildVFG() {
    outs() << "🌐 Building Value Flow Graph...\n";
    
    try {
        vfg = std::make_unique<SVF::VFG>(pta->getCallGraph());
        
        outs() << "✅ VFG built successfully\n";
        outs() << "📊 VFG Statistics:\n";
        outs() << "  VF nodes: " << vfg->getTotalNodeNum() << "\n";
        outs() << "  VF edges: " << vfg->getTotalEdgeNum() << "\n";
        
        return true;
    } catch (const std::exception& e) {
        outs() << "⚠️  VFG construction failed: " << e.what() << "\n";
        return false;
    }
}

const SVF::Function* SVFInterruptAnalyzer::findSVFFunction(const std::string& name) {
    if (!svfir) return nullptr;
    
    // 简化实现：在这个版本的SVF中，我们可能不需要直接获取SVF::Function
    // 因为我们主要通过LLVM Function和SVF的NodeID来工作
    return nullptr;
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
// 中断处理函数分析
//===----------------------------------------------------------------------===//

std::vector<InterruptHandlerResult> SVFInterruptAnalyzer::analyzeInterruptHandlers(const std::vector<std::string>& handler_names) {
    std::vector<InterruptHandlerResult> results;
    
    if (!svf_initialized) {
        errs() << "❌ SVF not initialized\n";
        return results;
    }
    
    outs() << "🔍 Analyzing " << handler_names.size() << " interrupt handlers...\n";
    outs() << std::string(60, '=') << "\n";
    
    for (const auto& name : handler_names) {
        outs() << "🎯 Analyzing handler: " << name << "\n";
        
        Function* handler = findFunction(name);
        if (!handler) {
            outs() << "  ❌ Function not found in loaded modules\n";
            
            // 创建空结果记录
            InterruptHandlerResult result;
            result.function_name = name;
            result.analysis_complete = false;
            results.push_back(result);
            continue;
        }
        
        outs() << "  ✅ Function found in module: " << handler->getParent()->getName() << "\n";
        
        InterruptHandlerResult result = analyzeSingleHandler(handler);
        results.push_back(result);
        
        outs() << "  📊 Analysis summary:\n";
        outs() << "    Instructions: " << result.total_instructions << "\n";
        outs() << "    Basic blocks: " << result.total_basic_blocks << "\n";
        outs() << "    Function calls: " << result.function_calls << "\n";
        outs() << "    Indirect calls: " << result.indirect_calls << "\n";
        outs() << "    Indirect call targets: " << result.indirect_call_targets.size() << "\n";
        outs() << "    Confidence: " << result.confidence_score << "/100\n";
        outs() << std::string(40, '-') << "\n";
    }
    
    outs() << "✅ All handlers analyzed\n";
    return results;
}

InterruptHandlerResult SVFInterruptAnalyzer::analyzeSingleHandler(Function* handler) {
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
        
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                result.function_calls++;
                if (!CI->getCalledFunction()) {
                    result.indirect_calls++;
                }
                
                // 记录被调用的函数
                if (CI->getCalledFunction()) {
                    result.called_functions.push_back(CI->getCalledFunction()->getName().str());
                }
            }
            
            if (isa<LoadInst>(&I) || isa<StoreInst>(&I)) {
                result.memory_operations++;
            }
            
            // 检查全局变量访问
            for (auto& Op : I.operands()) {
                if (auto* GV = dyn_cast<GlobalVariable>(Op)) {
                    std::string gv_name = GV->getName().str();
                    if (std::find(result.accessed_global_variables.begin(), 
                                 result.accessed_global_variables.end(), gv_name) == 
                        result.accessed_global_variables.end()) {
                        result.accessed_global_variables.push_back(gv_name);
                    }
                }
            }
        }
    }
    
    // SVF分析
#ifdef SVF_AVAILABLE
    if (svf_initialized) {
        result.indirect_call_targets = analyzeIndirectCalls(handler);
        result.pointer_analysis = analyzePointers(handler);
    }
#endif
    
    // 检测中断处理特征
    detectInterruptFeatures(handler, result);
    
    // 计算置信度
    result.confidence_score = calculateConfidence(result);
    result.analysis_complete = true;
    
    return result;
}

//===----------------------------------------------------------------------===//
// SVF分析方法 - 使用正确的LLVMModuleSet API
//===----------------------------------------------------------------------===//

#ifdef SVF_AVAILABLE
std::vector<std::string> SVFInterruptAnalyzer::analyzeIndirectCalls(Function* handler) {
    std::vector<std::string> targets;
    
    if (!pta || !svfir) return targets;
    
    // 获取LLVMModuleSet实例来访问hasValueNode和getValueNode
    SVF::LLVMModuleSet* moduleSet = SVF::LLVMModuleSet::getLLVMModuleSet();
    
    // 使用正确的SVF API
    for (auto& BB : *handler) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (!CI->getCalledFunction()) {
                    // 这是间接调用，使用SVF分析
                    Value* calledValue = CI->getCalledOperand();
                    
                    // 使用LLVMModuleSet检查是否有对应的节点
                    if (moduleSet->hasValueNode(calledValue)) {
                        SVF::NodeID callNodeId = moduleSet->getValueNode(calledValue);
                        const SVF::PointsTo& pts = pta->getPts(callNodeId);
                        
                        // 获取可能的调用目标
                        for (auto ptd : pts) {
                            const SVF::PAGNode* targetNode = svfir->getGNode(ptd);
                            if (const SVF::FunValVar* funVar = SVF::SVFUtil::dyn_cast<SVF::FunValVar>(targetNode)) {
                                targets.push_back(funVar->getFunction()->getName());
                            }
                        }
                    } else {
                        // 如果SVF中没有对应节点，使用基础启发式分析
                        if (auto* arg = dyn_cast<Argument>(calledValue)) {
                            targets.push_back("function_pointer_arg_" + std::to_string(arg->getArgNo()));
                        } else if (auto* gv = dyn_cast<GlobalVariable>(calledValue)) {
                            targets.push_back("global_func_ptr_" + gv->getName().str());
                        } else {
                            targets.push_back("unknown_indirect_call");
                        }
                    }
                }
            }
        }
    }
    
    return targets;
}

std::map<std::string, std::vector<std::string>> SVFInterruptAnalyzer::analyzePointers(Function* handler) {
    std::map<std::string, std::vector<std::string>> pointer_info;
    
    if (!pta || !svfir) return pointer_info;
    
    // 获取LLVMModuleSet实例
    SVF::LLVMModuleSet* moduleSet = SVF::LLVMModuleSet::getLLVMModuleSet();
    
    // 分析函数中的指针
    for (auto& BB : *handler) {
        for (auto& I : BB) {
            if (I.getType()->isPointerTy()) {
                // 使用LLVMModuleSet检查是否有对应的节点
                if (moduleSet->hasValueNode(&I)) {
                    SVF::NodeID nodeId = moduleSet->getValueNode(&I);
                    const SVF::PointsTo& pts = pta->getPts(nodeId);
                    
                    std::vector<std::string> pointed_objects;
                    for (auto ptd : pts) {
                        const SVF::PAGNode* objNode = svfir->getGNode(ptd);
                        // 简化处理，不使用可能不存在的hasValue/getValue方法
                        pointed_objects.push_back("obj_" + std::to_string(ptd));
                    }
                    
                    if (!pointed_objects.empty()) {
                        std::string ptr_name = getInstructionInfo(&I);
                        pointer_info[ptr_name] = pointed_objects;
                    }
                }
            }
        }
    }
    
    return pointer_info;
}
#else
std::vector<std::string> SVFInterruptAnalyzer::analyzeIndirectCalls(Function* handler) {
    return std::vector<std::string>();
}

std::map<std::string, std::vector<std::string>> SVFInterruptAnalyzer::analyzePointers(Function* handler) {
    return std::map<std::string, std::vector<std::string>>();
}
#endif

//===----------------------------------------------------------------------===//
// 特征检测
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::detectInterruptFeatures(Function* handler, InterruptHandlerResult& result) {
    // 检测设备访问模式
    for (auto& BB : *handler) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (CI->getCalledFunction()) {
                    std::string func_name = CI->getCalledFunction()->getName().str();
                    
                    if (isDeviceRelatedFunction(func_name)) {
                        result.has_device_access = true;
                    }
                    
                    if (isInterruptRelatedFunction(func_name)) {
                        result.has_irq_operations = true;
                    }
                    
                    // 检测工作队列操作
                    if (func_name.find("queue_work") != std::string::npos ||
                        func_name.find("schedule_work") != std::string::npos) {
                        result.has_work_queue_ops = true;
                    }
                }
            }
            
            // 检测参数访问模式（中断处理函数的典型模式）
            if (auto* LI = dyn_cast<LoadInst>(&I)) {
                Value* ptr = LI->getPointerOperand();
                if (auto* arg = dyn_cast<Argument>(ptr)) {
                    if (arg->getParent() == handler && arg->getArgNo() == 1) {
                        result.has_device_access = true;  // 通常第二个参数是设备数据
                    }
                }
            }
        }
    }
}

//===----------------------------------------------------------------------===//
// 辅助函数
//===----------------------------------------------------------------------===//

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

std::string SVFInterruptAnalyzer::getInstructionInfo(const Instruction* inst) {
    std::string info = inst->getOpcodeName();
    if (inst->hasName()) {
        info += "_" + inst->getName().str();
    }
    return info;
}

double SVFInterruptAnalyzer::calculateConfidence(const InterruptHandlerResult& result) {
    double score = 50.0;  // 基础分数
    
    // 根据分析完整性加分
    if (result.analysis_complete) score += 10.0;
    if (!result.indirect_call_targets.empty()) score += 15.0;
    if (!result.pointer_analysis.empty()) score += 10.0;
    
    // 根据中断处理特征加分
    if (result.has_irq_operations) score += 15.0;
    if (result.has_device_access) score += 10.0;
    if (result.has_work_queue_ops) score += 5.0;
    
    // 根据函数复杂度调整
    if (result.total_instructions > 0) {
        if (result.total_instructions > 100) score += 5.0;
        if (result.function_calls > 5) score += 5.0;
    }
    
    return std::min(score, 100.0);
}

//===----------------------------------------------------------------------===//
// 输出和统计
//===----------------------------------------------------------------------===//

void SVFInterruptAnalyzer::outputResults(const std::vector<InterruptHandlerResult>& results, const std::string& output_file) {
    json::Object root;
    json::Array handlers;
    
    for (const auto& result : results) {
        json::Object handler;
        
        handler["function_name"] = result.function_name;
        handler["source_file"] = result.source_file;
        handler["module_file"] = result.module_file;
        handler["total_instructions"] = (int64_t)result.total_instructions;
        handler["total_basic_blocks"] = (int64_t)result.total_basic_blocks;
        handler["function_calls"] = (int64_t)result.function_calls;
        handler["indirect_calls"] = (int64_t)result.indirect_calls;
        handler["memory_operations"] = (int64_t)result.memory_operations;
        handler["has_device_access"] = result.has_device_access;
        handler["has_irq_operations"] = result.has_irq_operations;
        handler["has_work_queue_ops"] = result.has_work_queue_ops;
        handler["analysis_complete"] = result.analysis_complete;
        handler["confidence_score"] = result.confidence_score;
        
        // 间接调用目标
        json::Array targets;
        for (const auto& target : result.indirect_call_targets) {
            targets.push_back(target);
        }
        handler["indirect_call_targets"] = std::move(targets);
        
        // 被调用函数
        json::Array called_funcs;
        for (const auto& func : result.called_functions) {
            called_funcs.push_back(func);
        }
        handler["called_functions"] = std::move(called_funcs);
        
        // 全局变量访问
        json::Array globals;
        for (const auto& gv : result.accessed_global_variables) {
            globals.push_back(gv);
        }
        handler["accessed_global_variables"] = std::move(globals);
        
        // 指针分析
        json::Object pointers;
        for (const auto& pair : result.pointer_analysis) {
            json::Array objects;
            for (const auto& obj : pair.second) {
                objects.push_back(obj);
            }
            pointers[pair.first] = std::move(objects);
        }
        handler["pointer_analysis"] = std::move(pointers);
        
        handlers.push_back(std::move(handler));
    }
    
    root["interrupt_handlers"] = std::move(handlers);
    root["total_handlers"] = (int64_t)results.size();
    root["analysis_timestamp"] = (int64_t)std::time(nullptr);
    root["analyzer_version"] = "SVF-1.0";
    
    // 统计信息
    size_t successful = 0;
    double avg_confidence = 0.0;
    for (const auto& result : results) {
        if (result.analysis_complete) successful++;
        avg_confidence += result.confidence_score;
    }
    if (!results.empty()) avg_confidence /= results.size();
    
    json::Object stats;
    stats["successful_analyses"] = (int64_t)successful;
    stats["average_confidence"] = avg_confidence;
    stats["total_modules_loaded"] = (int64_t)modules.size();
    
    root["statistics"] = std::move(stats);
    
    // 写入文件
    std::error_code EC;
    raw_fd_ostream OS(output_file, EC);
    if (EC) {
        errs() << "❌ Error writing to " << output_file << ": " << EC.message() << "\n";
        return;
    }
    
    OS << formatv("{0:2}", json::Value(std::move(root))) << "\n";
    outs() << "📄 Results written to: " << output_file << "\n";
}

void SVFInterruptAnalyzer::printStatistics() const {
    outs() << "\n📈 SVF Interrupt Analyzer Statistics\n";
    outs() << "====================================\n";
    outs() << "Loaded modules: " << modules.size() << "\n";
    outs() << "Loaded bitcode files: " << loaded_bc_files.size() << "\n";
    outs() << "SVF initialized: " << (svf_initialized ? "Yes" : "No") << "\n";
    
#ifdef SVF_AVAILABLE
    if (svf_initialized && svfir) {
        outs() << "\nSVFIR Statistics:\n";
        outs() << "  Total nodes: " << svfir->getTotalNodeNum() << "\n";
        outs() << "  Total edges: " << svfir->getTotalEdgeNum() << "\n";
        outs() << "  Value nodes: " << svfir->getValueNodeNum() << "\n";
        
        // 使用正确的API获取对象节点数量
        SVF::LLVMModuleSet* moduleSet = SVF::LLVMModuleSet::getLLVMModuleSet();
        outs() << "  Object nodes: " << moduleSet->getObjNodeNum() << "\n";
    }
    
    if (pta) {
        outs() << "\nPointer Analysis Statistics:\n";
        outs() << "  Analysis completed successfully\n";
    }
    
    if (vfg) {
        outs() << "\nVFG Statistics:\n";
        outs() << "  VF nodes: " << vfg->getTotalNodeNum() << "\n";
        outs() << "  VF edges: " << vfg->getTotalEdgeNum() << "\n";
    }
#endif
}
