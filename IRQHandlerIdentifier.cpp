//===- IRQHandlerIdentifier.cpp - Interrupt Handler Identifier Implementation ===//

#include "IRQHandlerIdentifier.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Instructions.h"
#include <algorithm>

using namespace llvm;

bool InterruptHandlerIdentifier::parseHandlerJsonFile(const std::string& json_file) {
    outs() << "📋 Parsing handler.json: " << json_file << "\n";
    
    // 检查文件是否存在
    if (!sys::fs::exists(json_file)) {
        errs() << "❌ File does not exist: " << json_file << "\n";
        return false;
    }
    
    // 清空现有数据
    clear();
    
    // 读取文件
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr = 
        MemoryBuffer::getFile(json_file);
    
    if (std::error_code EC = BufferOrErr.getError()) {
        errs() << "❌ Error reading handler.json: " << EC.message() << "\n";
        return false;
    }
    
    // 解析JSON
    Expected<json::Value> JsonOrErr = json::parse(BufferOrErr.get()->getBuffer());
    if (!JsonOrErr) {
        errs() << "❌ Error parsing handler.json: " << toString(JsonOrErr.takeError()) << "\n";
        return false;
    }
    
    json::Value &Json = *JsonOrErr;
    json::Object *RootObj = Json.getAsObject();
    if (!RootObj) {
        errs() << "❌ Expected JSON object in handler.json\n";
        return false;
    }
    
    // 获取combinations数组
    json::Array *CombinationsArray = RootObj->getArray("combinations");
    if (!CombinationsArray) {
        errs() << "❌ Expected 'combinations' array in handler.json\n";
        return false;
    }
    
    outs() << "✅ Found " << CombinationsArray->size() << " handler combinations\n";
    
    // 使用set来自动去重
    std::set<std::string> unique_handlers;
    std::set<std::string> unique_thread_fns;
    
    // 解析每个组合
    for (const json::Value &Combination : *CombinationsArray) {
        const json::Object *CombObj = Combination.getAsObject();
        if (!CombObj) continue;
        
        total_entries++;
        
        HandlerCombination combo;
        
        // 获取handler名称
        if (auto Handler = CombObj->getString("handler")) {
            combo.handler = std::string(*Handler);
            
            // 检查handler是否重复
            if (unique_handlers.find(combo.handler) != unique_handlers.end()) {
                duplicate_count++;
                if (duplicate_count <= 10) { // 只显示前10个重复项
                    outs() << "  ⚠️  Duplicate handler: " << combo.handler << "\n";
                }
            } else {
                unique_handlers.insert(combo.handler);
                addHandlerName(combo.handler);
            }
        }
        
        // 获取thread_fn名称（可选）
        if (auto ThreadFn = CombObj->getString("thread_fn")) {
            combo.thread_fn = std::string(*ThreadFn);
            
            // 如果thread_fn不为空且不重复，也添加到处理函数列表
            if (!combo.thread_fn.empty() && 
                unique_thread_fns.find(combo.thread_fn) == unique_thread_fns.end()) {
                unique_thread_fns.insert(combo.thread_fn);
                addHandlerName(combo.thread_fn);
            }
        }
        
        // 只有当handler不为空时才添加组合
        if (!combo.handler.empty()) {
            combinations.push_back(combo);
        }
    }
    
    // 输出统计信息
    outs() << "📊 Handler parsing summary:\n";
    outs() << "  Total entries processed: " << total_entries << "\n";
    outs() << "  Unique handlers: " << unique_handlers.size() << "\n";
    outs() << "  Unique thread functions: " << unique_thread_fns.size() << "\n";
    outs() << "  Total unique functions: " << handler_names.size() << "\n";
    outs() << "  Duplicate entries: " << duplicate_count << "\n";
    
    if (duplicate_count > 10) {
        outs() << "  (Only first 10 duplicates shown)\n";
    }
    
    if (handler_names.empty()) {
        errs() << "❌ No valid handlers found in JSON file\n";
        return false;
    }
    
    // 显示解析到的处理函数
    outs() << "🎯 Target interrupt handlers:\n";
    for (size_t i = 0; i < handler_names.size(); ++i) {
        outs() << "  [" << (i+1) << "] " << handler_names[i] << "\n";
    }
    
    return true;
}

bool InterruptHandlerIdentifier::loadHandlersFromJson(const std::string& json_file, Module &M) {
    // 首先解析JSON文件
    if (!parseHandlerJsonFile(json_file)) {
        return false;
    }
    
    outs() << "🔍 Searching for handlers in module: " << M.getName() << "\n";
    
    // 在模块中查找对应的函数
    int found_handlers = 0;
    int missing_handlers = 0;
    
    for (const auto& handler_name : handler_names) {
        Function *handler_func = findFunctionByName(M, handler_name);
        if (handler_func) {
            // 验证函数是否符合中断处理函数特征
            if (validateInterruptHandler(handler_func)) {
                identified_handlers.insert(handler_func);
                found_handlers++;
                outs() << "  ✅ Found and validated: " << handler_name << "\n";
            } else {
                outs() << "  ⚠️  Found but validation failed: " << handler_name << "\n";
                missing_handlers++;
            }
        } else {
            missing_handlers++;
            outs() << "  ❌ Not found: " << handler_name << "\n";
        }
    }
    
    // 输出识别结果统计
    outs() << "📊 Handler identification summary:\n";
    outs() << "  ✅ Found and validated: " << found_handlers << " / " << handler_names.size() << "\n";
    outs() << "  ❌ Missing or invalid: " << missing_handlers << " / " << handler_names.size() << "\n";
    
    return found_handlers > 0; // 至少找到一个处理函数才算成功
}

std::map<std::string, Function*> InterruptHandlerIdentifier::findHandlersInModules(
    const std::vector<std::unique_ptr<Module>>& modules) {
    
    std::map<std::string, Function*> found_handlers;
    
    if (handler_names.empty()) {
        outs() << "⚠️  No handler names to search for\n";
        return found_handlers;
    }
    
    outs() << "🔍 Searching for " << handler_names.size() << " handlers in " 
           << modules.size() << " modules...\n";
    
    for (const auto& handler_name : handler_names) {
        outs() << "Looking for: " << handler_name << "\n";
        
        bool found = false;
        for (const auto& M : modules) {
            Function* handler_func = findFunctionByName(*M, handler_name);
            if (handler_func) {
                if (validateInterruptHandler(handler_func)) {
                    found_handlers[handler_name] = handler_func;
                    identified_handlers.insert(handler_func);
                    outs() << "  ✅ Found in module: " << M->getName() << "\n";
                    found = true;
                    break;
                } else {
                    outs() << "  ⚠️  Found but validation failed in: " << M->getName() << "\n";
                }
            }
        }
        
        if (!found) {
            outs() << "  ❌ Not found in any module: " << handler_name << "\n";
        }
    }
    
    outs() << "📊 Multi-module search summary:\n";
    outs() << "  ✅ Found: " << found_handlers.size() << " / " << handler_names.size() << "\n";
    outs() << "  ❌ Missing: " << (handler_names.size() - found_handlers.size()) << "\n";
    
    return found_handlers;
}

Function* InterruptHandlerIdentifier::findFunctionByName(Module &M, const std::string& func_name) {
    for (auto &F : M) {
        if (F.getName() == func_name) {
            return &F;
        }
    }
    return nullptr;
}

bool InterruptHandlerIdentifier::validateInterruptHandler(Function *F) const {
    if (!F) return false;
    
    // 基本验证：函数不应该是声明
    if (F->isDeclaration()) {
        return false;
    }
    
    // 检查函数签名：中断处理函数通常有特定的参数模式
    // 典型的中断处理函数: int handler(int irq, void* dev_id)
    // 或者: irqreturn_t handler(int irq, void* dev_id)
    
    // 检查参数数量（通常是2个）
    if (F->arg_size() != 2) {
        // 允许某些特殊情况，如单参数或三参数的处理函数
        if (F->arg_size() < 1 || F->arg_size() > 3) {
            return false;
        }
    }
    
    // 检查返回类型（通常是整数类型）
    Type* return_type = F->getReturnType();
    if (!return_type->isIntegerTy() && !return_type->isVoidTy()) {
        return false;
    }
    
    // 检查第一个参数类型（通常是int，表示IRQ号）
    if (F->arg_size() >= 1) {
        Type* first_arg_type = F->getArg(0)->getType();
        if (!first_arg_type->isIntegerTy()) {
            return false;
        }
    }
    
    // 检查第二个参数类型（通常是void*，表示设备数据）
    if (F->arg_size() >= 2) {
        Type* second_arg_type = F->getArg(1)->getType();
        if (!second_arg_type->isPointerTy()) {
            return false;
        }
    }
    
    // 检查函数体是否包含典型的中断处理代码模式
    bool has_irq_pattern = false;
    
    for (auto& BB : *F) {
        for (auto& I : BB) {
            if (auto* CI = dyn_cast<CallInst>(&I)) {
                if (CI->getCalledFunction()) {
                    std::string func_name = CI->getCalledFunction()->getName().str();
                    
                    // 检查是否调用了典型的中断相关函数
                    if (func_name.find("irq") != std::string::npos ||
                        func_name.find("disable") != std::string::npos ||
                        func_name.find("enable") != std::string::npos ||
                        func_name.find("wake_up") != std::string::npos ||
                        func_name.find("schedule") != std::string::npos ||
                        func_name.find("spin_lock") != std::string::npos ||
                        func_name.find("spin_unlock") != std::string::npos) {
                        has_irq_pattern = true;
                        break;
                    }
                }
            }
            
            // 检查是否访问了典型的设备寄存器或数据结构
            if (auto* LI = dyn_cast<LoadInst>(&I)) {
                Value* ptr = LI->getPointerOperand();
                if (auto* GEP = dyn_cast<GetElementPtrInst>(ptr)) {
                    if (auto* struct_type = dyn_cast<StructType>(GEP->getSourceElementType())) {
                        std::string struct_name = struct_type->getName().str();
                        if (struct_name.find("device") != std::string::npos ||
                            struct_name.find("pci_dev") != std::string::npos ||
                            struct_name.find("net_device") != std::string::npos) {
                            has_irq_pattern = true;
                            break;
                        }
                    }
                }
            }
        }
        
        if (has_irq_pattern) break;
    }
    
    // 如果没有找到明显的中断处理模式，仍然可能是有效的处理函数
    // 特别是对于简单的或者优化过的处理函数
    return true; // 采用宽松的验证策略
}

void InterruptHandlerIdentifier::addHandlerName(const std::string& name) {
    // 避免重复添加
    if (std::find(handler_names.begin(), handler_names.end(), name) == handler_names.end()) {
        handler_names.push_back(name);
    }
}

void InterruptHandlerIdentifier::printStatistics() const {
    outs() << "\n📈 Interrupt Handler Identifier Statistics\n";
    outs() << "==========================================\n";
    outs() << "Total entries processed: " << total_entries << "\n";
    outs() << "Unique handler names: " << handler_names.size() << "\n";
    outs() << "Handler combinations: " << combinations.size() << "\n";
    outs() << "Identified functions: " << identified_handlers.size() << "\n";
    outs() << "Duplicate entries: " << duplicate_count << "\n";
    
    if (!handler_names.empty()) {
        outs() << "\nHandler Names:\n";
        for (size_t i = 0; i < handler_names.size(); ++i) {
            outs() << "  [" << (i+1) << "] " << handler_names[i] << "\n";
        }
    }
    
    if (!combinations.empty()) {
        outs() << "\nHandler Combinations:\n";
        for (size_t i = 0; i < combinations.size() && i < 10; ++i) {
            const auto& combo = combinations[i];
            outs() << "  [" << (i+1) << "] " << combo.handler;
            if (!combo.thread_fn.empty()) {
                outs() << " -> " << combo.thread_fn;
            }
            outs() << "\n";
        }
        if (combinations.size() > 10) {
            outs() << "  ... and " << (combinations.size() - 10) << " more\n";
        }
    }
}
