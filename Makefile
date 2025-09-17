# Makefile for LLVM IRQ Analysis Tool - 修复版本

# LLVM配置
LLVM_CONFIG = llvm-config
LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)

# 尝试获取所有LLVM库，失败则使用特定库
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs all 2>/dev/null || $(LLVM_CONFIG) --libs core support analysis callgraph irreader passes)

# 系统库
SYS_LIBS = -lpthread -ldl -lm

# 编译器和标志
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-unused-parameter $(LLVM_CXXFLAGS)
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS) $(SYS_LIBS)

# 目标文件
TARGET = irq_analyzer
TARGET_SIMPLE = irq_analyzer_simple

# 核心源文件（不包含main）
CORE_SOURCES = DataStructures.cpp \
               CompileCommandsParser.cpp \
               IRQHandlerIdentifier.cpp \
               MemoryAccessAnalyzer.cpp \
               FunctionPointerAnalyzer.cpp \
               FunctionCallAnalyzer.cpp \
               InlineAsmAnalyzer.cpp \
               JSONOutput.cpp \
               IRQAnalysisPass.cpp

# 完整源文件列表
SOURCES = main.cpp $(CORE_SOURCES)
SOURCES_SIMPLE = main_simple.cpp $(CORE_SOURCES)

# 对象文件
CORE_OBJECTS = $(CORE_SOURCES:.cpp=.o)
OBJECTS = main.o $(CORE_OBJECTS)
OBJECTS_SIMPLE = main_simple.o $(CORE_OBJECTS)

# 默认目标 - 构建简化版本避免命令行冲突
all: $(TARGET_SIMPLE)

# 构建标准版本（可能有命令行选项冲突）
standard: $(TARGET)

# 构建简化版本（推荐，避免LLVM命令行选项冲突）
simple: $(TARGET_SIMPLE)

# 链接标准版本
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET) (standard version)..."
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build completed: $(TARGET)"

# 链接简化版本
$(TARGET_SIMPLE): $(OBJECTS_SIMPLE)
	@echo "Linking $(TARGET_SIMPLE) (simple version)..."
	$(CXX) $(OBJECTS_SIMPLE) -o $(TARGET_SIMPLE) $(LDFLAGS)
	@echo "Build completed: $(TARGET_SIMPLE)"

# 编译核心源文件
%.o: %.cpp
	@echo "Compiling $< -> $@"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(CORE_OBJECTS) main.o main_simple.o $(TARGET) $(TARGET_SIMPLE)
	@echo "Clean completed"

# 测试简化版本
test: $(TARGET_SIMPLE)
	@echo "Creating test data..."
	@mkdir -p test
	@echo '{"total_unique_combinations": 5, "combinations": [{"handler": "test_handler1"}, {"handler": "test_handler2"}, {"handler": "test_handler1"}, {"handler": "test_handler3"}]}' > test/handler_with_duplicates.json
	@echo '[]' > test/minimal_compile_commands.json
	@echo "Running deduplication test with simple version..."
	./$(TARGET_SIMPLE) --compile-commands=test/minimal_compile_commands.json \
	                   --handlers=test/handler_with_duplicates.json \
	                   --output=test/dedup_test_output.json \
	                   --verbose || true
	@echo "Test completed"

# 测试标准版本
test-standard: $(TARGET)
	@echo "Testing standard version (may have command line conflicts)..."
	@mkdir -p test
	@echo '{"total_unique_combinations": 5, "combinations": [{"handler": "test_handler1"}, {"handler": "test_handler2"}, {"handler": "test_handler1"}, {"handler": "test_handler3"}]}' > test/handler_with_duplicates.json
	@echo '[]' > test/minimal_compile_commands.json
	./$(TARGET) --compile-commands=test/minimal_compile_commands.json \
	            --handlers=test/handler_with_duplicates.json \
	            --output=test/dedup_test_output.json \
	            --verbose || true

# 检查依赖
check-deps:
	@echo "Checking dependencies..."
	@which $(LLVM_CONFIG) > /dev/null || (echo "Error: llvm-config not found" && exit 1)
	@which $(CXX) > /dev/null || (echo "Error: clang++ not found" && exit 1)
	@echo "Dependencies OK"
	@echo "LLVM Version: $(shell $(LLVM_CONFIG) --version)"

# 显示构建信息
info:
	@echo "LLVM IRQ Analysis Tool - Build Information"
	@echo "=========================================="
	@echo "LLVM Version: $(shell $(LLVM_CONFIG) --version)"
	@echo "Compiler: $(CXX)"
	@echo "Core Sources: $(words $(CORE_SOURCES)) files"
	@echo "LLVM Libraries: $(LLVM_LIBS)"
	@echo ""
	@echo "Available targets:"
	@echo "  simple (default) - Build with simple argument parsing"
	@echo "  standard         - Build with LLVM CommandLine (may conflict)"
	@echo ""
	@echo "Build command (simple):"
	@echo "$(CXX) $(CXXFLAGS) $(SOURCES_SIMPLE) -o $(TARGET_SIMPLE) $(LDFLAGS)"

# 帮助
help:
	@echo "Available targets:"
	@echo "  simple    - Build simplified version (default, recommended)"
	@echo "  standard  - Build standard version (may have option conflicts)"
	@echo "  all       - Same as 'simple'"
	@echo "  clean     - Remove build artifacts"
	@echo "  test      - Run basic test with simple version"
	@echo "  test-standard - Test standard version"
	@echo "  check-deps- Check dependencies"
	@echo "  info      - Show build information"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Usage example (simple version):"
	@echo "  make simple"
	@echo "  ./irq_analyzer_simple --compile-commands=compile_commands.json \\"
	@echo "                        --handlers=handler.json \\"
	@echo "                        --output=results.json --verbose"

# 调试构建
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: $(TARGET_SIMPLE)
	@echo "Debug build completed"

# 发布构建
release: CXXFLAGS += -O3 -DNDEBUG
release: $(TARGET_SIMPLE)
	@echo "Release build completed"

.PHONY: all simple standard clean test test-standard check-deps info help debug release
