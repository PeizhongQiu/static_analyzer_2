#!/bin/bash

# 主项目创建脚本 - 便携式SVF测试项目 (修复版)
PROJECT_DIR="test_kernel_module"

echo "=== 创建便携式SVF分析器测试项目 ==="
echo "版本: 便携式 - 无内核头文件依赖"
echo

# 检查脚本文件
SCRIPTS_DIR="project_scripts"
if [ ! -d "$SCRIPTS_DIR" ]; then
    echo "❌ 错误: 找不到项目脚本目录: $SCRIPTS_DIR"
    echo "请先运行 setup_all.sh 创建所有必要的脚本文件"
    exit 1
fi

# 检查必需的脚本文件
echo "检查脚本文件..."
REQUIRED_SCRIPTS="create_headers.sh create_sources.sh create_drivers.sh create_configs.sh create_docs.sh"

for script in $REQUIRED_SCRIPTS; do
    if [ ! -f "$SCRIPTS_DIR/$script" ]; then
        echo "❌ 缺少脚本文件: $SCRIPTS_DIR/$script"
        exit 1
    fi
done
echo "✅ 所有脚本文件检查完成"
echo

# 清理并创建目录
if [ -d "$PROJECT_DIR" ]; then
    echo "删除已存在的项目目录: $PROJECT_DIR"
    rm -rf "$PROJECT_DIR"
fi

echo "创建项目目录结构..."
mkdir -p "$PROJECT_DIR"
mkdir -p "$PROJECT_DIR/include"
mkdir -p "$PROJECT_DIR/src"
mkdir -p "$PROJECT_DIR/drivers"

# 验证目录创建
if [ ! -d "$PROJECT_DIR/include" ] || [ ! -d "$PROJECT_DIR/src" ] || [ ! -d "$PROJECT_DIR/drivers" ]; then
    echo "❌ 错误: 目录创建失败"
    exit 1
fi

echo "✅ 目录结构创建成功"
echo

# 设置项目目录为绝对路径
PROJECT_PATH="$(pwd)/$PROJECT_DIR"

# 1. 创建头文件
echo "1. 创建头文件..."
cd "$SCRIPTS_DIR"
if bash create_headers.sh "$PROJECT_PATH"; then
    echo "✅ 头文件创建完成"
else
    echo "❌ 头文件创建失败"
    exit 1
fi
cd ..
echo

# 2. 创建源文件
echo "2. 创建源文件..."
cd "$SCRIPTS_DIR"
if bash create_sources.sh "$PROJECT_PATH"; then
    echo "✅ 源文件创建完成"
else
    echo "❌ 源文件创建失败"
    exit 1
fi
cd ..

# 2.1 创建中断处理函数文件 (单独处理)
echo "2.1 创建中断处理函数文件..."
cd "$SCRIPTS_DIR"
if [ -f "create_interrupt_handlers.sh" ]; then
    if bash create_interrupt_handlers.sh "$PROJECT_PATH"; then
        echo "✅ 中断处理函数文件创建完成"
    else
        echo "❌ 中断处理函数文件创建失败"
        exit 1
    fi
else
    echo "⚠️  create_interrupt_handlers.sh 不存在，跳过"
fi
cd ..
echo

# 3. 创建驱动文件
echo "3. 创建驱动文件..."
cd "$SCRIPTS_DIR"
if bash create_drivers.sh "$PROJECT_PATH"; then
    echo "✅ 驱动文件创建完成"
else
    echo "❌ 驱动文件创建失败"
    exit 1
fi
cd ..
echo

# 4. 创建配置文件
echo "4. 创建配置文件..."
cd "$SCRIPTS_DIR"
if bash create_configs.sh "$PROJECT_PATH"; then
    echo "✅ 配置文件创建完成"
else
    echo "❌ 配置文件创建失败"
    exit 1
fi
cd ..
echo

# 5. 创建文档文件
echo "5. 创建文档文件..."
cd "$SCRIPTS_DIR"
if bash create_docs.sh "$PROJECT_PATH"; then
    echo "✅ 文档文件创建完成"
else
    echo "❌ 文档文件创建失败"
    exit 1
fi
cd ..
echo

# 6. 设置权限
echo "6. 设置文件权限..."
if [ -f "$PROJECT_DIR/build_and_test.sh" ]; then
    chmod +x "$PROJECT_DIR/build_and_test.sh"
    echo "✅ 权限设置完成"
else
    echo "⚠️  build_and_test.sh 未找到，跳过权限设置"
fi
echo

# 7. 验证项目结构
echo "7. 验证项目结构..."
echo "项目文件统计:"
HEADER_COUNT=$(find "$PROJECT_DIR/include" -name "*.h" 2>/dev/null | wc -l)
SOURCE_COUNT=$(find "$PROJECT_DIR/src" -name "*.c" 2>/dev/null | wc -l)
DRIVER_COUNT=$(find "$PROJECT_DIR/drivers" -name "*.c" 2>/dev/null | wc -l)
CONFIG_COUNT=$(ls "$PROJECT_DIR"/*.json "$PROJECT_DIR"/Makefile 2>/dev/null | wc -l)

echo "   头文件: $HEADER_COUNT 个"
echo "   源文件: $SOURCE_COUNT 个"
echo "   驱动文件: $DRIVER_COUNT 个"
echo "   配置文件: $CONFIG_COUNT 个"
echo

# 项目创建完成
echo "=== 项目创建完成 ==="
echo
echo "📁 项目位置: $PROJECT_DIR"
echo "📊 项目特性:"
echo "   🔧 便携式设计 - 无内核头文件依赖"
echo "   🔧 复杂指针操作 - 多级指针、函数指针数组"
echo "   🔧 数据结构操作 - 嵌套结构、链表遍历"
echo "   🔧 内存读写分离 - 寄存器、缓冲区、原子操作"
echo "   🔧 4个中断处理函数 - 主要分析目标"
echo
echo "🚀 下一步操作:"
echo "   1. cd $PROJECT_DIR"
echo "   2. ./build_and_test.sh"
echo "   3. cd .. && ./enhanced_svf_irq_analyzer \\"
echo "      --compile-commands=$PROJECT_DIR/compile_commands.json \\"
echo "      --handlers=$PROJECT_DIR/handler.json \\"
echo "      --output=test_results.json --verbose"
echo
echo "📊 预期分析结果:"
echo "   • 内存操作: 约30-50个读写操作"
echo "   • 数据结构访问: 约20-30个结构体字段访问"
echo "   • 函数调用: 约15-25个函数调用"
echo "   • 全局变量修改: 约5-10个全局变量修改"
echo "   • 函数指针: 约3-5个函数指针目标"
echo
echo "💡 这个便携式版本应该能在任何有clang的系统上运行！"
