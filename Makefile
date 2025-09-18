# Makefile for LLVM IRQ Analysis Tool - Simplified Version

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
	@echo ""
	@echo "Key improvements in this version:"
	@echo "  ✓ Supports static functions (lowercase 't' symbols)"
	@echo "  ✓ Enhanced static variable detection"
	@echo "  ✓ Improved memory access analysis"

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
	@echo "Running basic test..."
	./$(TARGET_SIMPLE) --compile-commands=test/minimal_compile_commands.json \
	                   --handlers=test/handler_with_duplicates.json \
	                   --output=test/basic_test_output.json \
	                   --verbose || true
	@echo "Test completed"

# 测试ACPI处理函数（如果存在）
test-acpi: $(TARGET_SIMPLE)
	@echo "Testing ACPI handler analysis (if available)..."
	@KERNEL_DIR="../kafl.linux"; \
	BC_FILE="$$KERNEL_DIR/drivers/acpi/ec.bc"; \
	if [ -f "$$BC_FILE" ]; then \
		echo "Found ACPI bitcode file: $$BC_FILE"; \
		echo '{"total_unique_combinations": 1, "combinations": [{"handler": "acpi_ec_irq_handler"}]}' > test_acpi_handler.json; \
		echo '[{"directory": "'$$KERNEL_DIR'/drivers/acpi", "command": "clang -emit-llvm -c ec.c -o ec.bc", "file": "ec.c"}]' > test_acpi_compile_commands.json; \
		./$(TARGET_SIMPLE) --compile-commands=test_acpi_compile_commands.json \
		                   --handlers=test_acpi_handler.json \
		                   --output=acpi_test_results.json \
		                   --verbose; \
		echo "ACPI test completed - check acpi_test_results.json"; \
		rm -f test_acpi_handler.json test_acpi_compile_commands.json; \
	else \
		echo "ACPI bitcode file not found at $$BC_FILE"; \
		echo "To test with ACPI:"; \
		echo "  1. Compile ACPI EC driver: cd $$KERNEL_DIR/drivers/acpi && clang -emit-llvm -c ec.c -o ec.bc -I../../include -D__KERNEL__"; \
		echo "  2. Run: make test-acpi"; \
	fi

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
	@echo ""
	@echo "Available targets:"
	@echo "  simple (default) - Build with simple argument parsing"
	@echo "  standard         - Build with LLVM CommandLine (may conflict)"
	@echo ""
	@echo "Key features:"
	@echo "  • Supports static functions (lowercase 't' LLVM symbols)"
	@echo "  • Static variable access detection and analysis"
	@echo "  • Enhanced memory access pattern recognition"
	@echo "  • Device-related access identification"
	@echo "  • Comprehensive JSON output for fuzzing"
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
	@echo "  test      - Run basic test"
	@echo "  test-acpi - Test with ACPI handler (if available)"
	@echo "  check-deps- Check dependencies"
	@echo "  info      - Show build information"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Usage example:"
	@echo "  make simple"
	@echo "  ./irq_analyzer_simple --compile-commands=compile_commands.json \\"
	@echo "                        --handlers=handler.json \\"
	@echo "                        --output=results.json --verbose"
	@echo ""
	@echo "What's new:"
	@echo "  ✓ Now supports static interrupt handlers (functions with 't' linkage)"
	@echo "  ✓ Can find modifications to static variables in handlers"
	@echo "  ✓ Enhanced analysis for better fuzzing target identification"

# 调试构建
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: $(TARGET_SIMPLE)
	@echo "Debug build completed"

# 发布构建
release: CXXFLAGS += -O3 -DNDEBUG
release: $(TARGET_SIMPLE)
	@echo "Release build completed"

.PHONY: all simple standard clean test test-acpi check-deps info help debug release
