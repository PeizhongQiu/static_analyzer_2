# SVF Interrupt Handler Analyzer - Updated Makefile

# LLVM Configuration
LLVM_CONFIG = llvm-config
LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs core support analysis irreader bitreader)

# SVF Configuration
SVF_ROOT ?= /opt/svf-llvm14
SVF_AVAILABLE := $(shell test -f $(SVF_ROOT)/include/SVF-LLVM/LLVMUtil.h && echo 1 || echo 0)

# Compiler and flags - 添加线程支持
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wno-unused-parameter $(LLVM_CXXFLAGS) -pthread
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS) -lpthread -ldl -lm

# Target
TARGET = svf_irq_analyzer

# SVF Integration with updated include paths
ifeq ($(SVF_AVAILABLE),1)
    CXXFLAGS += -DSVF_AVAILABLE -I$(SVF_ROOT)/include
    CXXFLAGS += -fexceptions -frtti
    # Updated library linking order - core libraries first
    LDFLAGS += -L$(SVF_ROOT)/lib -lSvfLLVM -lSvfCore
    SVF_STATUS = Available
else
    SVF_STATUS = Not_Available
endif

# Source files - 添加并行分析器
SOURCES = main.cpp \
          SVFInterruptAnalyzer.cpp \
          ParallelSVFAnalyzer.cpp \
          CompileCommandsParser.cpp \
          IRQHandlerIdentifier.cpp

OBJECTS = $(SOURCES:.cpp=.o)

# Default target
all: info check-svf $(TARGET)

# Build info
info:
	@echo "SVF Interrupt Handler Analyzer"
	@echo "=============================="
	@echo "Target: $(TARGET)"
	@echo "LLVM: $(shell $(LLVM_CONFIG) --version)"
	@echo "SVF Status: $(SVF_STATUS)"
	@echo "SVF Root: $(SVF_ROOT)"
	@echo "Source files: $(words $(SOURCES))"
	@echo ""

# Enhanced SVF availability check
check-svf:
ifeq ($(SVF_AVAILABLE),0)
	@echo "❌ Error: SVF not found at $(SVF_ROOT)"
	@echo "Please install SVF or set SVF_ROOT environment variable"
	@echo "Example: export SVF_ROOT=/path/to/svf"
	@exit 1
else
	@echo "✅ SVF found at $(SVF_ROOT)"
	@echo "Checking libraries and headers..."
	@test -f $(SVF_ROOT)/lib/libSvfCore.a || (echo "❌ libSvfCore.a not found" && exit 1)
	@test -f $(SVF_ROOT)/lib/libSvfLLVM.a || (echo "❌ libSvfLLVM.a not found" && exit 1)
	@test -f $(SVF_ROOT)/include/SVFIR/SVFIR.h || (echo "❌ SVFIR.h not found" && exit 1)
	@test -f $(SVF_ROOT)/include/SVF-LLVM/LLVMModule.h || (echo "❌ LLVMModule.h not found" && exit 1)
	@echo "✅ Required libraries and headers found"
endif

# Build target with enhanced error handling
$(TARGET): $(OBJECTS)
	@echo "🔗 Linking $(TARGET)..."
	@echo "Using LDFLAGS: $(LDFLAGS)"
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "✅ Build completed: $(TARGET)"

# Compile rules with better error reporting
%.o: %.cpp
	@echo "🔨 Compiling $<..."
	@echo "Using CXXFLAGS: $(CXXFLAGS)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	@echo "🧹 Cleaning..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "✅ Clean completed"

# Install
install: $(TARGET)
	@echo "📦 Installing $(TARGET)..."
	sudo cp $(TARGET) /usr/local/bin/
	@echo "✅ Installed to /usr/local/bin/$(TARGET)"

# Uninstall
uninstall:
	@echo "🗑️  Uninstalling $(TARGET)..."
	sudo rm -f /usr/local/bin/$(TARGET)
	@echo "✅ Uninstalled"

# Setup SVF external API file
setup-svf:
	@echo "🔗 Setting up SVF external API..."
	@ln -sf /home/qpz/lab/SVF/Release-build/lib/extapi.bc ./extapi.bc
	@echo "✅ SVF setup completed"

# Test with sample data (with SVF setup)
test: $(TARGET) setup-svf
	@echo "🧪 Running test..."
	@if [ -f "compile_commands.json" ] && [ -f "handler.json" ]; then \
		./$(TARGET) --compile-commands=compile_commands.json \
		            --handlers=handler.json \
		            --output=test_results.json \
		            --verbose; \
		echo "✅ Test completed. Results in test_results.json"; \
	else \
		echo "❌ Test requires compile_commands.json and handler.json"; \
		echo "Please provide these files to run the test"; \
	fi

# Debug build with AddressSanitizer
debug-asan: CXXFLAGS += -g -O0 -DDEBUG -fsanitize=address -fno-omit-frame-pointer
debug-asan: LDFLAGS += -fsanitize=address
debug-asan: clean $(TARGET)
	@echo "🐛 Debug build with AddressSanitizer completed"

# Release build
release: CXXFLAGS += -O3 -DNDEBUG
release: clean $(TARGET)
	@echo "🚀 Release build completed"

# Enhanced dependency check
check-deps:
	@echo "🔍 Checking dependencies..."
	@which $(LLVM_CONFIG) > /dev/null || (echo "❌ llvm-config not found" && exit 1)
	@which $(CXX) > /dev/null || (echo "❌ clang++ not found" && exit 1)
	@echo "LLVM Version: $(shell $(LLVM_CONFIG) --version)"
	@echo "LLVM CXX Flags: $(shell $(LLVM_CONFIG) --cxxflags | cut -c1-80)..."
	@echo "LLVM LD Flags: $(shell $(LLVM_CONFIG) --ldflags | cut -c1-80)..."
	@echo "LLVM Libraries: $(shell $(LLVM_CONFIG) --libs | cut -c1-80)..."
	@echo "Compiler: $(shell $(CXX) --version | head -1)"
	@echo "SVF Status: $(SVF_STATUS)"
ifeq ($(SVF_AVAILABLE),1)
	@echo "SVF Include: $(SVF_ROOT)/include"
	@echo "SVF Lib: $(SVF_ROOT)/lib"
	@echo "✅ All dependencies OK"
else
	@echo "⚠️  SVF not available"
endif

# Show usage
help:
	@echo "SVF Interrupt Handler Analyzer"
	@echo "=============================="
	@echo ""
	@echo "Build Commands:"
	@echo "  make all      - Build the analyzer"
	@echo "  make clean    - Clean build files"
	@echo "  make debug    - Build debug version"
	@echo "  make release  - Build optimized version"
	@echo "  make test     - Run test (requires sample files)"
	@echo "  make install  - Install to /usr/local/bin"
	@echo "  make check-deps - Check dependencies"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET) --compile-commands=<file> --handlers=<file> [options]"
	@echo ""
	@echo "Required:"
	@echo "  --compile-commands=<file>   compile_commands.json file"
	@echo "  --handlers=<file>           handler.json file"
	@echo ""
	@echo "Optional:"
	@echo "  --output=<file>             Output JSON file"
	@echo "  --max-modules=<n>           Maximum modules to analyze"
	@echo "  --verbose                   Verbose output"
	@echo "  --help                      Show help"
	@echo ""
	@echo "Examples:"
	@echo "  ./$(TARGET) --compile-commands=cc.json --handlers=h.json"
	@echo "  ./$(TARGET) --compile-commands=cc.json --handlers=h.json --verbose"
	@echo ""
	@echo "Environment Variables:"
	@echo "  SVF_ROOT=<path>    Set SVF installation path (default: /opt/svf-llvm14)"

# Verbose build for debugging compilation issues
verbose: CXXFLAGS += -v
verbose: LDFLAGS += -v
verbose: clean 
	@echo "🔧 Verbose build mode"
	@$(MAKE) $(TARGET)

# Check specific SVF components
check-svf-detailed:
	@echo "🔍 Detailed SVF check..."
	@echo "SVF_ROOT: $(SVF_ROOT)"
	@echo "Checking include directories:"
	@ls -la $(SVF_ROOT)/include/ 2>/dev/null || echo "Include directory not found"
	@echo "Checking lib directories:"
	@ls -la $(SVF_ROOT)/lib/ 2>/dev/null || echo "Lib directory not found"
	@echo "Checking specific headers:"
	@test -f $(SVF_ROOT)/include/SVFIR/SVFIR.h && echo "✅ SVFIR.h found" || echo "❌ SVFIR.h missing"
	@test -f $(SVF_ROOT)/include/SVF-LLVM/LLVMModule.h && echo "✅ LLVMModule.h found" || echo "❌ LLVMModule.h missing"
	@test -f $(SVF_ROOT)/include/SVF-LLVM/SVFIRBuilder.h && echo "✅ SVFIRBuilder.h found" || echo "❌ SVFIRBuilder.h missing"
	@test -f $(SVF_ROOT)/include/WPA/Andersen.h && echo "✅ Andersen.h found" || echo "❌ Andersen.h missing"
	@test -f $(SVF_ROOT)/include/Graphs/VFG.h && echo "✅ VFG.h found" || echo "❌ VFG.h missing"

# Format code (requires clang-format)
format:
	@echo "🎨 Formatting code..."
	@find . -name "*.cpp" -o -name "*.h" | xargs clang-format -i -style=LLVM
	@echo "✅ Code formatted"

# Analyze code (requires clang-tidy)
analyze:
	@echo "🔍 Analyzing code..."
	@clang-tidy $(SOURCES) -- $(CXXFLAGS)
	@echo "✅ Code analysis completed"

# Generate documentation (requires doxygen)
docs:
	@echo "📚 Generating documentation..."
	@doxygen Doxyfile 2>/dev/null || echo "⚠️  Doxygen not found or Doxyfile missing"
	@echo "✅ Documentation generated"

# Count lines of code
loc:
	@echo "📊 Lines of code:"
	@wc -l $(SOURCES) *.h | tail -1

# Show compilation database
show-compile-db:
	@echo "📋 Compilation settings:"
	@echo "CXX: $(CXX)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "Sources: $(SOURCES)"
	@echo "Objects: $(OBJECTS)"

.PHONY: all info check-svf clean install uninstall test debug release check-deps help format analyze docs loc verbose check-svf-detailed show-compile-db

.DEFAULT_GOAL := all
