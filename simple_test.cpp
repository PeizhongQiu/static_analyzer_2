#include "IRQHandlerIdentifier.h"
#include "CompileCommandsParser.h"
#include <iostream>

int main() {
    std::cout << "=== Simple Test ===" << std::endl;
    
    // 1. 测试CompileCommandsParser
    std::cout << "1. Testing CompileCommandsParser..." << std::endl;
    CompileCommandsParser parser;
    if (parser.parseFromFile("../kafl.linux/compile_commands.json")) {
        std::cout << "✅ CompileCommandsParser worked" << std::endl;
        std::cout << "Command count: " << parser.getCommandCount() << std::endl;
        
        auto bc_files = parser.getBitcodeFiles();
        std::cout << "Expected .bc files: " << bc_files.size() << std::endl;
        
        // 检查前几个文件是否存在
        int existing_count = 0;
        for (size_t i = 0; i < std::min(bc_files.size(), size_t(5)); ++i) {
            std::ifstream file(bc_files[i]);
            if (file.good()) {
                existing_count++;
                std::cout << "  ✅ " << bc_files[i] << std::endl;
            } else {
                std::cout << "  ❌ " << bc_files[i] << std::endl;
            }
        }
        std::cout << "Existing .bc files: " << existing_count << "/" << bc_files.size() << std::endl;
        
    } else {
        std::cout << "❌ CompileCommandsParser failed" << std::endl;
        return 1;
    }
    
    // 2. 测试IRQHandlerIdentifier
    std::cout << "\n2. Testing IRQHandlerIdentifier..." << std::endl;
    InterruptHandlerIdentifier identifier;
    
    if (identifier.parseHandlerJsonFile("handler.json")) {
        std::cout << "✅ parseHandlerJsonFile worked" << std::endl;
        std::cout << "Handler names: " << identifier.getHandlerNames().size() << std::endl;
        for (const auto& name : identifier.getHandlerNames()) {
            std::cout << "  - " << name << std::endl;
        }
    } else {
        std::cout << "❌ parseHandlerJsonFile failed" << std::endl;
        return 1;
    }
    
    // 3. 测试模块加载（创建虚拟模块）
    std::cout << "\n3. Testing with dummy module..." << std::endl;
    llvm::LLVMContext context;
    llvm::Module dummy_module("dummy", context);
    
    if (identifier.loadHandlersFromJson("handler.json", dummy_module)) {
        std::cout << "✅ loadHandlersFromJson worked with dummy module" << std::endl;
    } else {
        std::cout << "❌ loadHandlersFromJson failed even with dummy module" << std::endl;
        std::cout << "This means the issue is in the JSON parsing or module logic" << std::endl;
    }
    
    return 0;
}
