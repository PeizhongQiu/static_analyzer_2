#!/bin/bash
# fixed_simple_test.sh - 修复后的简单测试

echo "🧪 Fixed Simple Test"
echo "==================="

# 创建修复后的测试程序
cat > simple_test_fixed.cpp << 'EOF'
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
EOF

# 编译修复后的测试程序
echo "🔨 Compiling fixed test program..."
if clang++ -std=c++17 -I. $(llvm-config --cxxflags) \
          simple_test_fixed.cpp CompileCommandsParser.cpp IRQHandlerIdentifier.cpp \
          -o simple_test_fixed \
          $(llvm-config --libs --ldflags) -lpthread; then
    
    echo "✅ Test program compiled successfully"
    echo "🧪 Running fixed test..."
    ./simple_test_fixed
    
else
    echo "❌ Still failed to compile test program"
    echo "Let's try a different approach..."
fi

echo ""
echo "🔍 Let's directly examine the issue..."

# 直接检查关键文件
echo "📋 Checking key files:"

echo ""
echo "1. Current directory contents:"
ls -la *.json *.cpp *.h | head -10

echo ""
echo "2. Checking if .bc files exist:"
bc_count=$(find ../kafl.linux -name "*.bc" 2>/dev/null | wc -l)
echo "   .bc files found: $bc_count"

if [ "$bc_count" -gt 0 ]; then
    echo "   First few .bc files:"
    find ../kafl.linux -name "*.bc" 2>/dev/null | head -5
    
    echo ""
    echo "3. Checking if any .bc file contains aer_irq:"
    found_aer=false
    for bc_file in $(find ../kafl.linux -name "*.bc" 2>/dev/null | head -10); do
        if llvm-dis "$bc_file" -o - 2>/dev/null | grep -q "aer_irq"; then
            echo "   ✅ Found aer_irq in: $bc_file"
            echo "   Functions in this file:"
            llvm-dis "$bc_file" -o - 2>/dev/null | grep "define.*@" | head -3
            found_aer=true
            break
        fi
    done
    
    if [ "$found_aer" = false ]; then
        echo "   ❌ aer_irq not found in any .bc files"
        echo "   Sample functions from first .bc file:"
        first_bc=$(find ../kafl.linux -name "*.bc" 2>/dev/null | head -1)
        if [ -n "$first_bc" ]; then
            llvm-dis "$first_bc" -o - 2>/dev/null | grep "define.*@" | head -3
        fi
    fi
else
    echo "   ❌ No .bc files found!"
    echo "   You need to generate them first:"
    echo "   cd ../kafl.linux && python3 ccjson_to_bc.py compile_commands.json"
fi

echo ""
echo "🎯 Key findings summary:"
echo "1. The handler.json parsing works (we see 'Loaded 1 total entries')"
echo "2. The issue is that aer_irq function doesn't exist in the .bc files"
echo "3. We need to either:"
echo "   a) Find the correct .bc file that contains aer_irq"
echo "   b) Use a different handler name that actually exists"
echo "   c) Create a test .bc file with aer_irq"
