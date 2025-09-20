#include "IRQHandlerIdentifier.h"
#include "CompileCommandsParser.h"
#include <iostream>
#include <fstream>  // 添加缺失的头文件

int main() {
    std::cout << "=== Fixed Simple Test ===" << std::endl;
    
    // 1. 测试CompileCommandsParser
    std::cout << "1. Testing CompileCommandsParser..." << std::endl;
    CompileCommandsParser parser;
    if (parser.parseFromFile("../kafl.linux/compile_commands.json")) {
        std::cout << "✅ CompileCommandsParser worked" << std::endl;
        std::cout << "Command count: " << parser.getCommandCount() << std::endl;
        
        auto bc_files = parser.getBitcodeFiles();
        std::cout << "Expected .bc files: " << bc_files.size() << std::endl;
        
        // 显示前几个.bc文件路径
        for (size_t i = 0; i < std::min(bc_files.size(), size_t(5)); ++i) {
            std::cout << "  " << i+1 << ": " << bc_files[i] << std::endl;
        }
        
    } else {
        std::cout << "❌ CompileCommandsParser failed" << std::endl;
        return 1;
    }
    
    // 2. 测试IRQHandlerIdentifier
    std::cout << "\n2. Testing IRQHandlerIdentifier..." << std::endl;
    InterruptHandlerIdentifier identifier;
    
    std::cout << "2a. Testing parseHandlerJsonFile..." << std::endl;
    if (identifier.parseHandlerJsonFile("handler.json")) {
        std::cout << "✅ parseHandlerJsonFile worked" << std::endl;
        std::cout << "Handler names found: " << identifier.getHandlerNames().size() << std::endl;
        for (const auto& name : identifier.getHandlerNames()) {
            std::cout << "  - " << name << std::endl;
        }
    } else {
        std::cout << "❌ parseHandlerJsonFile failed" << std::endl;
        return 1;
    }
    
    // 3. 测试模块加载（创建虚拟模块）
    std::cout << "\n2b. Testing loadHandlersFromJson with dummy module..." << std::endl;
    llvm::LLVMContext context;
    llvm::Module dummy_module("dummy", context);
    
    if (identifier.loadHandlersFromJson("handler.json", dummy_module)) {
        std::cout << "✅ loadHandlersFromJson worked with dummy module" << std::endl;
        std::cout << "Identified handlers: " << identifier.getHandlerCount() << std::endl;
    } else {
        std::cout << "❌ loadHandlersFromJson failed with dummy module" << std::endl;
        std::cout << "This confirms the issue: no aer_irq function in dummy module" << std::endl;
    }
    
    return 0;
}
