//===- IRQHandlerIdentifier.cpp - Interrupt Handler Identifier Implementation ===//

#include "IRQHandlerIdentifier.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include <algorithm>
#include <set>

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
    
    // 使用set来自动去重，只关注handler
    std::set<std::string> unique_handlers;
    
    // 解析每个组合，只提取handler
    for (const json::Value &Combination : *CombinationsArray) {
        const json::Object *CombObj = Combination.getAsObject();
        if (!CombObj) continue;
        
        total_entries++;
        
        HandlerCombination combo;
        
        // 只获取handler名称，忽略thread_fn
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
            
            // 只有当handler不为空时才添加组合（不关心thread_fn）
            if (!combo.handler.empty()) {
                // thread_fn字段保持空值，不进行处理
                combinations.push_back(combo);
            }
        }
    }
    
    // 输出统计信息
    outs() << "📊 Handler parsing summary:\n";
    outs() << "  Total entries processed: " << total_entries << "\n";
    outs() << "  Unique handlers: " << unique_handlers.size() << "\n";
    outs() << "  Thread functions: 0 (ignored)\n";  // 明确说明忽略了thread_fn
    outs() << "  Total functions to analyze: " << handler_names.size() << "\n";
    outs() << "  Duplicate entries: " << duplicate_count << "\n";
    
    if (duplicate_count > 10) {
        outs() << "  (Only first 10 duplicates shown)\n";
    }
    
    if (handler_names.empty()) {
        errs() << "❌ No valid handlers found in JSON file\n";
        return false;
    }
    
    // 显示解析到的处理函数
    outs() << "🎯 Target interrupt handlers (thread_fn ignored):\n";
    for (size_t i = 0; i < handler_names.size(); ++i) {
        outs() << "  [" << (i+1) << "] " << handler_names[i] << "\n";
    }
    
    return true;
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
    outs() << "Duplicate entries: " << duplicate_count << "\n";
    outs() << "Thread functions analyzed: 0 (ignored by design)\n";
    
    if (!handler_names.empty()) {
        outs() << "\nHandler Names (only 'handler' field):\n";
        for (size_t i = 0; i < handler_names.size(); ++i) {
            outs() << "  [" << (i+1) << "] " << handler_names[i] << "\n";
        }
    }
    
    if (!combinations.empty()) {
        outs() << "\nHandler Combinations (thread_fn ignored):\n";
        for (size_t i = 0; i < combinations.size() && i < 10; ++i) {
            const auto& combo = combinations[i];
            outs() << "  [" << (i+1) << "] " << combo.handler;
            // 不显示thread_fn信息
            outs() << "\n";
        }
        if (combinations.size() > 10) {
            outs() << "  ... and " << (combinations.size() - 10) << " more\n";
        }
    }
    
    outs() << "\n📝 Note: This analysis focuses only on 'handler' functions.\n";
    outs() << "   'thread_fn' fields are ignored as they represent threaded\n";
    outs() << "   bottom-half processing, not the actual interrupt handlers.\n";
}
