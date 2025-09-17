# 测试去重功能
test-dedup: $(TARGET)
	@echo "Testing deduplication functionality..."
	@echo "Creating test handler.json with duplicates..."
	@mkdir -p test
	@echo '{"total_unique_combinations": 6, "combinations": [{"handler": "test_handler1"}, {"handler": "test_handler2"}, {"handler": "test_handler1"}, {"handler": "test_handler3"}, {"handler": "test_handler2"}]}' > test/handler_with_duplicates.json
	@echo "Expected: 3 unique handlers (test_handler1, test_handler2, test_handler3)"
	@echo "Expected: 2 duplicates detected"
	@echo ""
	@echo "Running analyzer with duplicate handlers..."
	@echo '{' > test/minimal_compile_commands.json
	@echo '  "combinations": []' >> test/minimal_compile_commands.json
	@echo '}' >> test/minimal_compile_commands.json
	@./$(TARGET) --compile-commands=test/minimal_compile_commands.json \
	            --handlers=test/handler_with_duplicates.json \
	            --output=test/dedup_test_output.json \
	            --verbose || true# Makefile for LLVM IRQ Analysis Tool

# LLVM配置
LLVM_CONFIG = llvm-config
LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs core support analysis callgraph)

# 编译器和标志
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra $(LLVM_CXXFLAGS)
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS)

# 目标文件
TARGET = irq_analyzer

# 源文件
SOURCES = main.cpp \
          DataStructures.cpp \
          CompileCommandsParser.cpp \
          IRQHandlerIdentifier.cpp \
          MemoryAccessAnalyzer.cpp \
          FunctionPointerAnalyzer.cpp \
          FunctionCallAnalyzer.cpp \
          InlineAsmAnalyzer.cpp \
          JSONOutput.cpp \
          IRQAnalysisPass.cpp

# 头文件
HEADERS = DataStructures.h \
          CompileCommandsParser.h \
          IRQHandlerIdentifier.h \
          MemoryAccessAnalyzer.h \
          FunctionPointerAnalyzer.h \
          FunctionCallAnalyzer.h \
          InlineAsmAnalyzer.h \
          JSONOutput.h \
          IRQAnalysisPass.h

# 对象文件
OBJECTS = $(SOURCES:.cpp=.o)

# 默认目标
all: $(TARGET)

# 链接目标
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build completed: $(TARGET)"

# 编译源文件
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean completed"

# 安装
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/
	@echo "Installation completed"

# 测试
test: $(TARGET)
	@echo "Running basic tests..."
	@if [ -f "test/compile_commands.json" ] && [ -f "test/handler.json" ]; then \
		./$(TARGET) --compile-commands=test/compile_commands.json \
		           --handlers=test/handler.json \
		           --output=test_output.json \
		           --verbose; \
	else \
		echo "No test data found."; \
		echo "Create test/compile_commands.json and test/handler.json for testing."; \
		echo ""; \
		echo "Example usage:"; \
		echo "./$(TARGET) --compile-commands=compile_commands.json \\"; \
		echo "             --handlers=handler.json \\"; \
		echo "             --output=results.json"; \
	fi

# 帮助
help:
	@echo "Available targets:"
	@echo "  all        - Build the analyzer (default)"
	@echo "  clean      - Remove build artifacts"
	@echo "  install    - Install to /usr/local/bin"
	@echo "  test       - Run basic tests"
	@echo "  test-dedup - Test handler deduplication functionality"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Usage example:"
	@echo "  make"
	@echo "  ./irq_analyzer --compile-commands=path/to/compile_commands.json \\"
	@echo "                 --handlers=path/to/handler.json \\"
	@echo "                 --output=results.json"
	@echo ""
	@echo "Required files:"
	@echo "  - compile_commands.json: LLVM bitcode compilation database"
	@echo "  - handler.json: Interrupt handler function definitions"
	@echo ""
	@echo "Features:"
	@echo "  - Automatic handler deduplication"
	@echo "  - Detailed memory access analysis"
	@echo "  - Function pointer resolution"
	@echo "  - JSON output for fuzzing integration"

# 依赖检查
check-deps:
	@echo "Checking dependencies..."
	@which $(LLVM_CONFIG) > /dev/null || (echo "Error: llvm-config not found" && exit 1)
	@which $(CXX) > /dev/null || (echo "Error: clang++ not found" && exit 1)
	@echo "Dependencies OK"

# 调试构建
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: $(TARGET)
	@echo "Debug build completed"

# 发布构建
release: CXXFLAGS += -O3 -DNDEBUG
release: $(TARGET)
	@echo "Release build completed"

# 生成文档
doc:
	@if which doxygen > /dev/null; then \
		doxygen Doxyfile; \
		echo "Documentation generated in doc/html/"; \
	else \
		echo "Doxygen not found. Install doxygen to generate documentation."; \
	fi

.PHONY: all clean install test help check-deps debug release doc test-dedup
