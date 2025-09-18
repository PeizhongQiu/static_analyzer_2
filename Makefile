# Cross-Module IRQ Analyzer - Simple Makefile

# LLVM Configuration
LLVM_CONFIG = llvm-config
LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs all 2>/dev/null || $(LLVM_CONFIG) --libs core support analysis irreader)

# Compiler and flags
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wno-unused-parameter $(LLVM_CXXFLAGS)
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS) -lpthread -ldl -lm

# Target
TARGET = irq_analyzer_cross_module

# Source files
SOURCES = main_cross_module.cpp \
          CrossModuleAnalyzer.cpp \
          HandlerAnalysis.cpp \
          DataFlowAnalyzer.cpp \
          DeepFunctionPointerAnalyzer.cpp \
          EnhancedMemoryAnalyzer.cpp \
          DataStructures.cpp \
          CompileCommandsParser.cpp \
          IRQHandlerIdentifier.cpp \
          MemoryAccessAnalyzer.cpp \
          InlineAsmAnalyzer.cpp \
          JSONOutput.cpp

OBJECTS = $(SOURCES:.cpp=.o)

# Default target
all: $(TARGET)

# Build executable
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build completed: $(TARGET)"

# Compile source files
%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean completed"

# Basic test
test: $(TARGET)
	@echo "Creating test files..."
	@mkdir -p test
	@echo '{"total_unique_combinations": 2, "combinations": [{"handler": "test_handler"}, {"handler": "acpi_handler"}]}' > test/handler.json
	@echo '[{"directory": ".", "command": "clang -c test.c", "file": "test.c"}]' > test/compile_commands.json
	@echo "Running test..."
	./$(TARGET) --compile-commands=test/compile_commands.json --handlers=test/handler.json --output=test/results.json --verbose || true
	@echo "Test completed"

# Check dependencies
check:
	@echo "Checking dependencies..."
	@which $(LLVM_CONFIG) > /dev/null || (echo "Error: llvm-config not found" && exit 1)
	@which $(CXX) > /dev/null || (echo "Error: clang++ not found" && exit 1)
	@echo "LLVM Version: $(shell $(LLVM_CONFIG) --version)"
	@echo "Dependencies OK"

# Show build info
info:
	@echo "Cross-Module IRQ Analyzer"
	@echo "Target: $(TARGET)"
	@echo "Sources: $(words $(SOURCES)) files"
	@echo "LLVM Version: $(shell $(LLVM_CONFIG) --version)"
	@echo "Compiler: $(CXX)"

# Debug build
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: clean $(TARGET)
	@echo "Debug build completed"

# Release build
release: CXXFLAGS += -O3 -DNDEBUG
release: clean $(TARGET)
	@echo "Release build completed"

# Help
help:
	@echo "Available targets:"
	@echo "  all     - Build $(TARGET) (default)"
	@echo "  clean   - Remove build files"
	@echo "  test    - Run basic test"
	@echo "  check   - Check dependencies"
	@echo "  info    - Show build information"
	@echo "  debug   - Build with debug info"
	@echo "  release - Build optimized version"
	@echo "  help    - Show this help"
	@echo ""
	@echo "Usage:"
	@echo "  make"
	@echo "  ./$(TARGET) --compile-commands=FILE --handlers=FILE [options]"

.PHONY: all clean test check info debug release help
