//===- IRQHandlerIdentifier.cpp - Interrupt Handler Identifier Implementation ===//

#include "IRQHandlerIdentifier.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

bool InterruptHandlerIdentifier::parseHandlerJsonFile(const std::string& json_file) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr = 
        MemoryBuffer::getFile(json_file);
    
    if (std::error_code EC = BufferOrErr.getError()) {
        errs() << "Error reading handler.json: " << EC.message() << "\n";
        return false;
    }
    
    Expected<json::Value> JsonOrErr = json::parse(BufferOrErr.get()->getBuffer());
    if (!JsonOrErr) {
        errs() << "Error parsing handler.json: " << toString(JsonOrErr.takeError()) << "\n";
        return false;
    }
    
    json::Value &Json = *JsonOrErr;
    json::Object *RootObj = Json.getAsObject();
    if (!RootObj) {
        errs() << "Expected JSON object in handler.json\n";
        return false;
    }
    
    // 获取combinations数组
    json::Array *CombinationsArray = RootObj->getArray("combinations");
    if (!CombinationsArray) {
        errs() << "Expected 'combinations' array in handler.json\n";
        return false;
    }
    
    // 使用set来自动去重
    std::set<std::string> unique_handlers;
    total_entries = 0;
    duplicate_count = 0;
    
    // 解析每个组合，只提取handler名称
    for (const json::Value &Combination : *CombinationsArray) {
        const json::Object *CombObj = Combination.getAsObject();
        if (!CombObj) continue;
        
        total_entries++;
        
        // 获取handler名称
        if (auto Handler = CombObj->getString("handler")) {
            std::string handler_name = std::string(*Handler);
            
            // 检查是否重复
            if (unique_handlers.find(handler_name) != unique_handlers.end()) {
                duplicate_count++;
                outs() << "Warning: Duplicate handler found: " << handler_name << "\n";
            } else {
                unique_handlers.insert(handler_name);
            }
        }
        // 忽略thread_fn字段
    }
    
    // 将去重后的结果转换为vector
    handler_names.assign(unique_handlers.begin(), unique_handlers.end());
    
    // 输出统计信息
    outs() << "Loaded " << total_entries << " total entries from " << json_file << "\n";
    outs() << "Found " << duplicate_count << " duplicate handlers\n";
    outs() << "Unique handlers after deduplication: " << handler_names.size() << "\n";
    
    return true;
}

Function* InterruptHandlerIdentifier::findFunctionByName(Module &M, const std::string& func_name) {
    for (auto &F : M) {
        if (F.getName() == func_name) {
            return &F;
        }
    }
    return nullptr;
}

bool InterruptHandlerIdentifier::loadHandlersFromJson(const std::string& json_file, Module &M) {
    // 首先解析JSON文件
    if (!parseHandlerJsonFile(json_file)) {
        return false;
    }
    
    // 在模块中查找对应的函数
    int found_handlers = 0;
    int missing_handlers = 0;
    
    for (const auto& handler_name : handler_names) {
        Function *handler_func = findFunctionByName(M, handler_name);
        if (handler_func) {
            identified_handlers.insert(handler_func);
            found_handlers++;
            outs() << "Found handler: " << handler_name << "\n";
        } else {
            missing_handlers++;
            outs() << "Warning: Handler not found in module: " << handler_name << "\n";
        }
    }
    
    // 输出统计结果
    outs() << "\nHandler identification summary:\n";
    outs() << "  Found handlers: " << found_handlers << " / " << handler_names.size() << "\n";
    outs() << "  Missing handlers: " << missing_handlers << "\n";
    
    return found_handlers > 0; // 至少找到一个处理函数才算成功
}
