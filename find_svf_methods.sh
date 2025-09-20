#!/bin/bash
# find_svf_methods.sh - 查找正确的SVF方法名称

echo "🔍 Finding Correct SVF Method Names"
echo "=================================="

SVF_INCLUDE="/opt/svf-llvm14/include"

echo "1. LLVMModuleSet methods:"
echo "========================"
if [ -f "$SVF_INCLUDE/SVF-LLVM/LLVMUtil.h" ]; then
    echo "📋 All LLVMModuleSet methods:"
    grep -n "class LLVMModuleSet" -A 50 "$SVF_INCLUDE/SVF-LLVM/LLVMUtil.h" | grep -E "(add|get|load|SVF)" | head -10
    echo ""
    echo "📋 Specific method search:"
    grep -rn "addModule\|loadModule\|getSVFValue\|SVFValue" "$SVF_INCLUDE/SVF-LLVM/" | head -5
fi

echo ""
echo "2. SVFIR value node methods:"
echo "============================"
if [ -f "$SVF_INCLUDE/SVFIR/SVFIR.h" ]; then
    echo "📋 Value node related methods:"
    grep -n -E "(Value|Node|SVF)" "$SVF_INCLUDE/SVFIR/SVFIR.h" | head -10
    echo ""
    echo "📋 Has/Get methods:"
    grep -n -E "(has.*Node|get.*Node)" "$SVF_INCLUDE/SVFIR/SVFIR.h" | head -5
fi

echo ""
echo "3. MemObj and getValue methods:"
echo "=============================="
echo "📋 Searching for MemObj class:"
find "$SVF_INCLUDE" -name "*.h" -exec grep -l "class.*MemObj\|struct.*MemObj" {} \; | head -3
echo ""
echo "📋 getValue method locations:"
find "$SVF_INCLUDE" -name "*.h" -exec grep -l "getValue" {} \; | head -5

echo ""
echo "4. SVFValue class details:"
echo "========================="
if [ -f "$SVF_INCLUDE/SVFIR/SVFValue.h" ]; then
    echo "📋 SVFValue class methods:"
    grep -n -A 20 "class SVFValue" "$SVF_INCLUDE/SVFIR/SVFValue.h" | head -15
fi

echo ""
echo "5. ValVar and ObjVar methods:"
echo "============================"
if [ -f "$SVF_INCLUDE/SVFIR/SVFVariables.h" ]; then
    echo "📋 ValVar methods:"
    grep -n -A 10 "class ValVar" "$SVF_INCLUDE/SVFIR/SVFVariables.h" | head -8
    echo ""
    echo "📋 ObjVar methods:"
    grep -n -A 10 "class ObjVar" "$SVF_INCLUDE/SVFIR/SVFVariables.h" | head -8
fi

echo ""
echo "6. Finding correct method signatures:"
echo "=================================="

# 搜索常见的方法名
echo "📋 Module loading methods:"
grep -rn "Module.*add\|load.*Module" "$SVF_INCLUDE/" 2>/dev/null | head -3

echo ""
echo "📋 SVFValue creation methods:"
grep -rn "SVFValue.*get\|get.*SVFValue" "$SVF_INCLUDE/" 2>/dev/null | head -3

echo ""
echo "📋 Node existence checking:"
grep -rn "Node.*has\|has.*Node" "$SVF_INCLUDE/" 2>/dev/null | head -3

echo ""
echo "📋 Value getting methods:"
grep -rn "getValue\|get.*Value" "$SVF_INCLUDE/SVFIR/" 2>/dev/null | head -5
