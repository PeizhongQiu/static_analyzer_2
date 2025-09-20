#!/bin/bash
# svf_header_analysis.sh - 分析SVF头文件以确定正确的API

echo "🔍 SVF Header File Analysis"
echo "=========================="

SVF_ROOT="/opt/svf-llvm14"
SVF_INCLUDE="$SVF_ROOT/include"

if [ ! -d "$SVF_INCLUDE" ]; then
    echo "❌ SVF include directory not found: $SVF_INCLUDE"
    exit 1
fi

echo "Analyzing SVF installation at: $SVF_ROOT"
echo ""

# 1. 基本信息
echo "1. SVF Installation Info:"
echo "   Include path: $SVF_INCLUDE"
echo "   Library path: $SVF_ROOT/lib"
echo "   Libraries found:"
ls -la "$SVF_ROOT/lib"/libSvf*.* 2>/dev/null || echo "   No SVF libraries found"
echo ""

# 2. 分析SVFIRBuilder.h
echo "2. SVFIRBuilder API Analysis:"
BUILDER_H="$SVF_INCLUDE/SVF-LLVM/SVFIRBuilder.h"
if [ -f "$BUILDER_H" ]; then
    echo "   ✅ Found: $BUILDER_H"
    
    echo "   📋 Constructor signatures:"
    grep -n "SVFIRBuilder(" "$BUILDER_H" | head -5
    
    echo "   📋 Build methods:"
    grep -n -E "(build|buildSVFIR)" "$BUILDER_H" | head -5
    
    echo "   📋 Class definition:"
    grep -n "class SVFIRBuilder" "$BUILDER_H"
    
else
    echo "   ❌ SVFIRBuilder.h not found"
fi
echo ""

# 3. 分析SVFIR.h
echo "3. SVFIR API Analysis:"
SVFIR_H="$SVF_INCLUDE/SVFIR/SVFIR.h"
if [ -f "$SVFIR_H" ]; then
    echo "   ✅ Found: $SVFIR_H"
    
    echo "   📋 Value node methods:"
    grep -n -E "(hasValueNode|getValueNode|getSVFValue)" "$SVFIR_H" | head -5
    
    echo "   📋 PAG methods:"
    grep -n "getPAG" "$SVFIR_H" | head -3
    
    echo "   📋 Node count methods:"
    grep -n -E "(getTotalNodeNum|getPAGNodeNum)" "$SVFIR_H" | head -3
    
else
    echo "   ❌ SVFIR.h not found"
    
    # 尝试查找PAG.h（可能是老版本）
    PAG_H="$SVF_INCLUDE/Graphs/PAG.h"
    if [ -f "$PAG_H" ]; then
        echo "   ✅ Found PAG.h instead (older SVF structure)"
    fi
fi
echo ""

# 4. 分析LLVMUtil.h
echo "4. LLVMUtil API Analysis:"
LLVM_UTIL_H="$SVF_INCLUDE/SVF-LLVM/LLVMUtil.h"
if [ -f "$LLVM_UTIL_H" ]; then
    echo "   ✅ Found: $LLVM_UTIL_H"
    
    echo "   📋 LLVMModuleSet methods:"
    grep -n "LLVMModuleSet" "$LLVM_UTIL_H" | head -5
    
    echo "   📋 SVFValue methods:"
    grep -n "getSVFValue" "$LLVM_UTIL_H" | head -3
    
else
    echo "   ❌ LLVMUtil.h not found"
fi
echo ""

# 5. 检查MemObj和相关类型
echo "5. Memory Object API Analysis:"
MEM_FILES=$(find "$SVF_INCLUDE" -name "*.h" -exec grep -l "class.*MemObj\|struct.*MemObj" {} \; 2>/dev/null)
if [ -n "$MEM_FILES" ]; then
    echo "   📁 MemObj defined in:"
    for file in $MEM_FILES; do
        echo "      $file"
        grep -n "class.*MemObj\|struct.*MemObj" "$file"
    done
    
    # 查看MemObj的方法
    echo "   📋 MemObj methods:"
    grep -n "getValue\|isFunction" $MEM_FILES | head -5
else
    echo "   ❌ MemObj class not found"
fi
echo ""

# 6. 检查VAR相关类
echo "6. Variable Node API Analysis:"
VAR_FILES=$(find "$SVF_INCLUDE" -name "*.h" -exec grep -l "class.*ValVar\|class.*ObjVar" {} \; 2>/dev/null)
if [ -n "$VAR_FILES" ]; then
    echo "   📁 Variable classes found in:"
    for file in $VAR_FILES; do
        echo "      $file"
    done
    
    echo "   📋 ValVar methods:"
    grep -n "getValue\|class ValVar" $VAR_FILES | head -3
    
    echo "   📋 ObjVar methods:"
    grep -n "getMemObj\|class ObjVar" $VAR_FILES | head -3
else
    echo "   ❌ Variable node classes not found"
fi
echo ""

# 7. 版本推断
echo "7. SVF Version Analysis:"
echo "==============================="

# 检查版本特征
HAS_LLVM_MODULE_SET=$(grep -r "LLVMModuleSet" "$SVF_INCLUDE" 2>/dev/null | wc -l)
HAS_SVF_VALUE=$(grep -r "class SVFValue\|struct SVFValue" "$SVF_INCLUDE" 2>/dev/null | wc -l)
HAS_BUILD_METHOD=$(grep -r "virtual.*build()" "$SVF_INCLUDE" 2>/dev/null | wc -l)

echo "API Feature Analysis:"
echo "   LLVMModuleSet references: $HAS_LLVM_MODULE_SET"
echo "   SVFValue class: $HAS_SVF_VALUE"
echo "   build() method: $HAS_BUILD_METHOD"

if [ "$HAS_LLVM_MODULE_SET" -gt 0 ] && [ "$HAS_SVF_VALUE" -gt 0 ]; then
    echo "   ✅ Modern SVF API detected (likely 2.x/3.x)"
    SVF_VERSION_TYPE="modern"
elif [ "$HAS_BUILD_METHOD" -gt 0 ]; then
    echo "   ✅ Intermediate SVF API detected"
    SVF_VERSION_TYPE="intermediate"
else
    echo "   ⚠️  Older SVF API detected"
    SVF_VERSION_TYPE="old"
fi

echo ""
echo "8. API Usage Recommendations:"
echo "============================"

case $SVF_VERSION_TYPE in
    "modern")
        echo "✅ For your SVF version, use:"
        echo "   1. SVFIRBuilder builder; (no arguments)"
        echo "   2. LLVMModuleSet::getLLVMModuleSet()->addModule(module)"
        echo "   3. builder.build() (no arguments)"
        echo "   4. LLVMModuleSet::getLLVMModuleSet()->getSVFValue(value)"
        echo "   5. svfir->hasValueNode(svfValue)"
        echo "   6. svfir->getValueNode(svfValue)"
        ;;
    "intermediate")
        echo "✅ For your SVF version, use:"
        echo "   1. SVFIRBuilder builder; (no arguments)"
        echo "   2. builder.build(module) (with module argument)"
        echo "   3. Check for getSVFValue vs hasValueNode"
        ;;
    "old")
        echo "⚠️  Older SVF version detected:"
        echo "   1. May need SVFIRBuilder(bool) constructor"
        echo "   2. May use buildSVFIR instead of build"
        echo "   3. Different value node API"
        ;;
esac

echo ""
echo "9. Next Steps:"
echo "============="
echo "1. Try compiling the API inspector:"
echo "   clang++ -std=c++17 -DSVF_AVAILABLE -I$SVF_INCLUDE \\"
echo "           svf_api_inspector.cpp -o svf_api_inspector"
echo ""
echo "2. If compilation fails, share the errors for API-specific fixes"
echo "3. If successful, run ./svf_api_inspector for runtime API testing"
