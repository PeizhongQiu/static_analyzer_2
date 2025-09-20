#!/bin/bash
# accurate_search.sh - 精确搜索aer_irq函数

echo "🔍 Accurate Search for aer_irq Function"
echo "======================================"

echo "📊 Total .bc files: $(find ../kafl.linux -name "*.bc" 2>/dev/null | wc -l)"

echo ""
echo "🔍 Method 1: Direct grep in all .bc files for aer_irq..."
echo "This may take a moment with 2849 files..."

# 更精确的搜索方法
found_files=()
search_count=0

for bc_file in $(find ../kafl.linux -name "*.bc" 2>/dev/null); do
    search_count=$((search_count + 1))
    if [ $((search_count % 500)) -eq 0 ]; then
        echo "   Searched $search_count files..."
    fi
    
    # 使用llvm-dis检查是否包含aer_irq
    if llvm-dis "$bc_file" -o - 2>/dev/null | grep -q "aer_irq"; then
        found_files+=("$bc_file")
        echo "✅ Found aer_irq in: $bc_file"
        
        # 显示该文件中的aer_irq相关内容
        echo "   📋 aer_irq related content:"
        llvm-dis "$bc_file" -o - 2>/dev/null | grep -A2 -B2 "aer_irq" | head -10
        
        break  # 找到第一个就停止
    fi
done

echo "🔍 Searched $search_count files"

if [ ${#found_files[@]} -eq 0 ]; then
    echo ""
    echo "🔍 Method 2: Search for files that might contain AER-related code..."
    
    # 搜索可能包含AER代码的文件
    echo "Looking for files with 'aer' in path or name..."
    find ../kafl.linux -path "*aer*" -name "*.bc" 2>/dev/null
    
    echo ""
    echo "Looking for PCI-related files that might contain aer_irq..."
    find ../kafl.linux -path "*pci*" -name "*.bc" 2>/dev/null | head -10
    
    echo ""
    echo "🔍 Method 3: Let's check a broader pattern..."
    echo "Searching for any function with 'aer' in the name..."
    
    aer_count=0
    for bc_file in $(find ../kafl.linux -name "*.bc" 2>/dev/null | head -100); do
        if llvm-dis "$bc_file" -o - 2>/dev/null | grep -q "define.*aer"; then
            echo "✅ Found AER-related function in: $bc_file"
            llvm-dis "$bc_file" -o - 2>/dev/null | grep "define.*aer"
            aer_count=$((aer_count + 1))
            if [ $aer_count -ge 3 ]; then
                break
            fi
        fi
    done
    
else
    echo ""
    echo "✅ aer_irq function found in ${#found_files[@]} file(s)!"
    echo ""
    echo "🚀 Now the analyzer should work! Let's test:"
    echo "   ./svf_irq_analyzer --compile-commands=../kafl.linux/compile_commands.json --handlers=handler.json --verbose"
    echo ""
    echo "📋 The issue might be that the analyzer isn't loading the specific .bc file that contains aer_irq"
fi

echo ""
echo "🔍 Method 4: Let's also check what CompileCommandsParser is actually looking for..."

# 创建简单测试程序来看CompileCommandsParser的输出
cat > check_parser.cpp << 'EOF'
#include "CompileCommandsParser.h"
#include <iostream>

int main() {
    CompileCommandsParser parser;
    if (parser.parseFromFile("../kafl.linux/compile_commands.json")) {
        auto bc_files = parser.getBitcodeFiles();
        std::cout << "CompileCommandsParser expects " << bc_files.size() << " .bc files" << std::endl;
        
        std::cout << "First 10 expected files:" << std::endl;
        for (size_t i = 0; i < std::min(bc_files.size(), size_t(10)); ++i) {
            std::cout << "  " << bc_files[i] << std::endl;
        }
        
        // 检查是否包含AER相关的文件
        std::cout << "\nLooking for AER-related files in expected list..." << std::endl;
        for (const auto& file : bc_files) {
            if (file.find("aer") != std::string::npos || 
                file.find("pci") != std::string::npos) {
                std::cout << "  📁 " << file << std::endl;
            }
        }
    }
    return 0;
}
EOF

echo "🔨 Compiling parser check..."
if clang++ -std=c++17 -I. $(llvm-config --cxxflags) \
          check_parser.cpp CompileCommandsParser.cpp \
          -o check_parser \
          $(llvm-config --libs --ldflags) -lpthread 2>/dev/null; then
    
    echo "🧪 Running parser check..."
    ./check_parser
else
    echo "❌ Could not compile parser check"
fi

echo ""
echo "💡 If aer_irq exists but analyzer doesn't find it, the issue might be:"
echo "1. CompileCommandsParser isn't including the right .bc file"
echo "2. The .bc file path is incorrect"
echo "3. The analyzer isn't actually loading all the .bc files it should"
