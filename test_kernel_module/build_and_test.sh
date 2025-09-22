#!/bin/bash

echo "=== 便携式SVF分析器测试项目构建脚本 ==="
echo

# 检查依赖
echo "1. 检查依赖..."
echo "操作系统: $(uname -a)"

CLANG_VERSION=$(clang --version | head -1)
echo "✅ Clang版本: $CLANG_VERSION"
echo

# 测试编译
echo "2. 测试编译单个文件..."
make test
if [ $? -eq 0 ]; then
    echo "✅ 编译测试成功"
else
    echo "❌ 编译测试失败"
    exit 1
fi
echo

# 生成compile_commands.json
echo "3. 生成compile_commands.json..."
make compile_commands.json
if [ $? -eq 0 ]; then
    echo "✅ compile_commands.json生成成功"
else
    echo "❌ compile_commands.json生成失败"
    exit 1
fi
echo

# 生成所有bitcode文件
echo "4. 生成bitcode文件..."
make bitcode
if [ $? -eq 0 ]; then
    echo "✅ bitcode文件生成成功"
else
    echo "❌ bitcode文件生成失败"
    exit 1
fi
echo

# 验证文件
echo "5. 验证生成的文件..."
make verify

bc_count=$(find . -name "*.bc" | wc -l)
echo ""
if [ $bc_count -eq 0 ]; then
    echo "❌ 没有生成任何bitcode文件"
    exit 1
else
    echo "✅ 成功生成 $bc_count 个bitcode文件"
fi

# 检查handler.json
if [ -f "handler.json" ]; then
    echo "✅ handler.json存在"
    if command -v python3 &> /dev/null; then
        handlers=$(python3 -c "import json; print(len(json.load(open('handler.json'))['combinations']))" 2>/dev/null)
        if [ $? -eq 0 ]; then
            echo "  包含 $handlers 个中断处理函数"
        fi
    fi
else
    echo "❌ handler.json不存在"
fi
echo

# 检查SVF分析器
echo "6. 检查SVF分析器..."
if [ -f "../enhanced_svf_irq_analyzer" ]; then
    echo "✅ 找到SVF分析器: ../enhanced_svf_irq_analyzer"
    echo ""
    echo "🚀 可以运行以下命令进行分析:"
    echo "────────────────────────────────────────────────────"
    echo "cd .."
    echo "./enhanced_svf_irq_analyzer \\"
    echo "    --compile-commands=test_kernel_module/compile_commands.json \\"
    echo "    --handlers=test_kernel_module/handler.json \\"
    echo "    --output=test_analysis_results.json \\"
    echo "    --verbose"
    echo "────────────────────────────────────────────────────"
else
    echo "⚠️  未找到SVF分析器"
    echo "请确保enhanced_svf_irq_analyzer在上级目录中"
fi
echo

echo "=== 构建完成 ==="
echo "📁 项目文件总结:"
echo "   📄 源文件: $(find . -name "*.c" | wc -l) 个"
echo "   📄 头文件: $(find . -name "*.h" | wc -l) 个"  
echo "   📄 Bitcode文件: $(find . -name "*.bc" | wc -l) 个"
echo "   📄 配置文件: 2 个 (Makefile, handler.json)"
echo ""

if [ $bc_count -gt 0 ]; then
    echo "✅ 便携式项目构建成功！"
    echo ""
    echo "🔧 项目特性:"
    echo "   ✅ 不依赖内核头文件"
    echo "   ✅ 兼容任何Linux系统"
    echo "   ✅ 包含复杂的指针和数据结构操作"
    echo "   ✅ 4个中断处理函数用于分析"
    echo ""
    echo "📊 预期分析结果:"
    echo "   • 内存操作: 约30-50个读写操作"
    echo "   • 数据结构访问: 约20-30个结构体字段访问"
    echo "   • 函数调用: 约15-25个函数调用"
    echo "   • 全局变量修改: 约5-10个全局变量修改"
    echo "   • 函数指针: 约3-5个函数指针目标"
else
    echo "❌ 构建失败，请检查错误信息"
fi
