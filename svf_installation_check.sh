#!/bin/bash
# SVF安装检查脚本

echo "🔍 SVF Installation Check"
echo "========================="

# 检查SVF_ROOT
echo "1. Checking SVF_ROOT environment..."
if [ -n "$SVF_ROOT" ]; then
    echo "   SVF_ROOT: $SVF_ROOT"
else
    echo "   SVF_ROOT: Not set"
fi

# 检查常见的SVF安装位置
echo ""
echo "2. Checking common SVF locations..."
locations=(
    "/opt/svf-llvm14"
    "/usr/local/svf"
    "/usr/local"
    "/opt/svf"
    "$HOME/svf"
    "$HOME/SVF"
)

for loc in "${locations[@]}"; do
    if [ -d "$loc" ]; then
        echo "   📁 $loc: EXISTS"
        
        # 检查include目录
        if [ -d "$loc/include/SVF-LLVM" ]; then
            echo "      ✅ Headers: $loc/include/SVF-LLVM/"
        elif [ -d "$loc/include" ]; then
            echo "      📁 Include: $loc/include/ (checking contents...)"
            find "$loc/include" -name "*SVF*" -type d 2>/dev/null | head -3
        fi
        
        # 检查lib目录
        if [ -d "$loc/lib" ]; then
            echo "      📁 Lib directory: $loc/lib/"
            # 查找SVF库文件
            svf_libs=$(find "$loc/lib" -name "*Svf*" -o -name "*svf*" 2>/dev/null)
            if [ -n "$svf_libs" ]; then
                echo "      ✅ SVF libraries found:"
                echo "$svf_libs" | sed 's/^/         /'
            else
                echo "      ❌ No SVF libraries found"
            fi
        fi
        echo ""
    else
        echo "   ❌ $loc: Not found"
    fi
done

# 检查系统范围的SVF安装
echo ""
echo "3. Checking system-wide SVF installation..."
find /usr -name "*Svf*" -o -name "*svf*" 2>/dev/null | grep -E "(lib|include)" | head -5
find /opt -name "*Svf*" -o -name "*svf*" 2>/dev/null | grep -E "(lib|include)" | head -5

# 检查pkg-config
echo ""
echo "4. Checking pkg-config..."
if command -v pkg-config >/dev/null 2>&1; then
    if pkg-config --exists svf 2>/dev/null; then
        echo "   ✅ SVF found via pkg-config"
        echo "      Cflags: $(pkg-config --cflags svf)"
        echo "      Libs: $(pkg-config --libs svf)"
    else
        echo "   ❌ SVF not found via pkg-config"
    fi
else
    echo "   ❌ pkg-config not available"
fi

# 检查cmake
echo ""
echo "5. Checking cmake find_package..."
if command -v cmake >/dev/null 2>&1; then
    temp_dir=$(mktemp -d)
    cat > "$temp_dir/CMakeLists.txt" << 'EOF'
cmake_minimum_required(VERSION 3.10)
project(svf_test)
find_package(SVF QUIET)
if(SVF_FOUND)
    message(STATUS "SVF found via cmake")
else()
    message(STATUS "SVF not found via cmake")
endif()
EOF
    cd "$temp_dir"
    cmake . 2>/dev/null | grep -i svf
    cd - >/dev/null
    rm -rf "$temp_dir"
else
    echo "   ❌ cmake not available"
fi

echo ""
echo "6. Summary and Recommendations:"
echo "==============================="

# 给出建议
if find /opt /usr/local -name "*Svf*" -o -name "*svf*" 2>/dev/null | grep -q lib; then
    echo "✅ SVF appears to be installed on your system"
    echo ""
    echo "🔧 Try these fixes:"
    echo "1. Find the exact SVF location:"
    echo "   find /opt /usr/local -name 'libSvf*' 2>/dev/null"
    echo ""
    echo "2. Set SVF_ROOT correctly:"
    echo "   export SVF_ROOT=/path/to/svf/root"
    echo ""
    echo "3. Check library names:"
    echo "   ls -la /path/to/svf/lib/lib*"
    
else
    echo "❌ SVF doesn't appear to be installed"
    echo ""
    echo "🚀 Install SVF:"
    echo "1. git clone https://github.com/SVF-tools/SVF.git"
    echo "2. cd SVF"
    echo "3. source ./build.sh"
    echo "4. export SVF_ROOT=\$PWD"
fi

echo ""
echo "🧪 After fixing, test with:"
echo "export SVF_ROOT=/correct/path/to/svf"
echo "make check-svf"
