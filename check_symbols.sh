#!/bin/bash

# 检查 bitcode 文件中的符号，对比 handler.json 中的函数名

KERNEL_DIR="../kafl.linux"
HANDLER_JSON="handler.json"

echo "检查 Bitcode 文件中的符号"
echo "========================="

# 查找所有 bitcode 文件
echo "1. 查找 bitcode 文件..."
BC_FILES=$(find "$KERNEL_DIR" -name "*.bc" -type f 2>/dev/null)
BC_COUNT=$(echo "$BC_FILES" | wc -l)

if [ "$BC_COUNT" -eq 0 ]; then
    echo "错误: 没有找到任何 .bc 文件"
    exit 1
fi

echo "找到 $BC_COUNT 个 bitcode 文件"
echo ""

# 提取 handler.json 中的函数名
echo "2. 从 handler.json 提取函数名..."
if [ ! -f "$HANDLER_JSON" ]; then
    echo "错误: 找不到 $HANDLER_JSON"
    exit 1
fi

HANDLERS=$(jq -r '.combinations[].handler' "$HANDLER_JSON" 2>/dev/null | sort | uniq)
HANDLER_COUNT=$(echo "$HANDLERS" | wc -l)
echo "需要查找的处理函数: $HANDLER_COUNT 个"
echo ""

# 检查每个 bitcode 文件中的符号
echo "3. 检查 bitcode 文件中的符号..."
echo ""

ALL_SYMBOLS=$(mktemp)
FOUND_HANDLERS=$(mktemp)

total_functions=0
matching_handlers=0

echo "$BC_FILES" | while IFS= read -r bc_file; do
    if [ -z "$bc_file" ]; then
        continue
    fi
    
    echo "检查文件: ${bc_file#$KERNEL_DIR/}"
    
    if ! command -v llvm-nm >/dev/null 2>&1; then
        echo "  警告: llvm-nm 不可用，无法检查符号"
        continue
    fi
    
    # 提取函数符号 (T = text/code section)
    symbols=$(llvm-nm "$bc_file" 2>/dev/null | grep ' T ' | awk '{print $3}' | sort)
    
    if [ -z "$symbols" ]; then
        echo "  没有找到函数符号"
        continue
    fi
    
    func_count=$(echo "$symbols" | wc -l)
    echo "  包含 $func_count 个函数"
    
    # 显示一些示例函数名
    echo "  示例函数:"
    echo "$symbols" | head -5 | sed 's/^/    /'
    
    # 检查是否包含我们要找的处理函数
    matches=0
    echo "$HANDLERS" | while IFS= read -r handler; do
        if [ -z "$handler" ]; then
            continue
        fi
        
        if echo "$symbols" | grep -q "^${handler}$"; then
            echo "  ✓ 找到: $handler"
            echo "$handler" >> "$FOUND_HANDLERS"
            ((matches++))
            ((matching_handlers++))
        fi
    done
    
    if [ "$matches" -eq 0 ]; then
        echo "  ✗ 没有找到匹配的处理函数"
        
        # 尝试模糊匹配
        echo "  相似的函数名:"
        echo "$HANDLERS" | while IFS= read -r handler; do
            if [ -n "$handler" ]; then
                # 查找包含处理函数名的符号
                similar=$(echo "$symbols" | grep -i "$handler" | head -3)
                if [ -n "$similar" ]; then
                    echo "    $handler -> $similar" | sed 's/^/    /'
                fi
            fi
        done
    fi
    
    # 保存所有符号到总列表
    echo "$symbols" >> "$ALL_SYMBOLS"
    ((total_functions += func_count))
    
    echo ""
done

# 统计结果
echo "4. 统计结果"
echo "==========="

unique_symbols=$(sort "$ALL_SYMBOLS" | uniq | wc -l)
found_handler_count=$(sort "$FOUND_HANDLERS" | uniq | wc -l)

echo "总函数符号: $unique_symbols"
echo "找到的处理函数: $found_handler_count / $HANDLER_COUNT"

if [ "$found_handler_count" -gt 0 ]; then
    echo ""
    echo "找到的处理函数:"
    sort "$FOUND_HANDLERS" | uniq | sed 's/^/  ✓ /'
fi

# 显示未找到的处理函数
missing_handlers=$(echo "$HANDLERS" | while IFS= read -r handler; do
    if [ -n "$handler" ] && ! grep -q "^${handler}$" "$FOUND_HANDLERS"; then
        echo "$handler"
    fi
done)

missing_count=$(echo "$missing_handlers" | grep -c .)

if [ "$missing_count" -gt 0 ]; then
    echo ""
    echo "未找到的处理函数 ($missing_count 个):"
    echo "$missing_handlers" | head -10 | sed 's/^/  ✗ /'
    
    if [ "$missing_count" -gt 10 ]; then
        echo "  ... 还有 $((missing_count - 10)) 个"
    fi
    
    echo ""
    echo "5. 分析未找到函数的可能原因"
    echo "=========================="
    
    # 分析一些未找到的函数
    echo "检查部分未找到函数的可能变体:"
    echo "$missing_handlers" | head -5 | while IFS= read -r missing; do
        if [ -n "$missing" ]; then
            echo ""
            echo "分析: $missing"
            
            # 查找相似的符号
            similar_symbols=$(sort "$ALL_SYMBOLS" | uniq | grep -i "$missing" || true)
            if [ -n "$similar_symbols" ]; then
                echo "  相似符号:"
                echo "$similar_symbols" | head -5 | sed 's/^/    /'
            else
                echo "  没有找到相似符号"
                
                # 尝试部分匹配
                partial_match=$(echo "$missing" | sed 's/[0-9]*$//')
                if [ "$partial_match" != "$missing" ]; then
                    partial_symbols=$(sort "$ALL_SYMBOLS" | uniq | grep -i "$partial_match" || true)
                    if [ -n "$partial_symbols" ]; then
                        echo "  部分匹配 ($partial_match):"
                        echo "$partial_symbols" | head -3 | sed 's/^/    /'
                    fi
                fi
            fi
        fi
    done
fi

# 生成建议
echo ""
echo "6. 建议"
echo "======"

if [ "$found_handler_count" -eq 0 ]; then
    echo "没有找到任何处理函数，可能的原因:"
    echo "1. handler.json 中的函数名不正确"
    echo "2. 需要编译包含这些函数的源文件"
    echo "3. 函数名在编译时被修改了"
    echo ""
    echo "建议操作:"
    echo "1. 检查实际的内核源码中的函数定义"
    echo "2. 运行 ./find_handlers.sh 查找函数源码位置"
    echo "3. 重新生成 handler.json 使用实际的函数名"
elif [ "$found_handler_count" -lt "$((HANDLER_COUNT / 2))" ]; then
    echo "只找到少量处理函数，建议:"
    echo "1. 编译更多包含处理函数的源文件"
    echo "2. 检查 handler.json 中函数名的准确性"
    echo "3. 使用模糊匹配更新函数名"
else
    echo "✓ 找到了大部分处理函数"
    echo "可以继续运行分析器，但建议:"
    echo "1. 补充缺失函数对应的源文件编译"
    echo "2. 更新 handler.json 移除不存在的函数"
fi

# 清理临时文件
rm -f "$ALL_SYMBOLS" "$FOUND_HANDLERS"
