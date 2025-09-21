# Enhanced SVF Interrupt Handler Analyzer - Makefile

# LLVM Configuration
LLVM_CONFIG = llvm-config
LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs core support analysis irreader bitreader)

# SVF Configuration
SVF_ROOT ?= /opt/svf-llvm14
SVF_AVAILABLE := $(shell test -f $(SVF_ROOT)/include/SVF-LLVM/LLVMUtil.h && echo 1 || echo 0)

# Compiler and flags - Enhanced with additional optimization
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wno-unused-parameter $(LLVM_CXXFLAGS) -pthread -g
CXXFLAGS += -O2 -DENHANCED_ANALYSIS
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS) -lpthread -ldl -lm

# Target
TARGET = enhanced_svf_irq_analyzer

# SVF Integration with enhanced features
ifeq ($(SVF_AVAILABLE),1)
    CXXFLAGS += -DSVF_AVAILABLE -I$(SVF_ROOT)/include
    CXXFLAGS += -fexceptions -frtti
    CXXFLAGS += -DENHANCED_SVF_FEATURES
    LDFLAGS += -L$(SVF_ROOT)/lib -lSvfLLVM -lSvfCore
    SVF_STATUS = Available_Enhanced
else
    SVF_STATUS = Not_Available
endif

# Source files - 精简版本
SOURCES = main.cpp \
          SVFInterruptAnalyzer.cpp \
          MemoryAnalyzer.cpp \
          DataStructureAnalyzer.cpp \
          FunctionPointerAnalyzer.cpp \
          AnalysisOutputManager.cpp \
          CompileCommandsParser.cpp \
          IRQHandlerIdentifier.cpp

OBJECTS = $(SOURCES:.cpp=.o)

# Default target
all: info check-svf $(TARGET)

# Build info
info:
	@echo "Enhanced SVF Interrupt Handler Analyzer"
	@echo "======================================="
	@echo "Target: $(TARGET)"
	@echo "LLVM: $(shell $(LLVM_CONFIG) --version)"
	@echo "SVF Status: $(SVF_STATUS)"
	@echo "SVF Root: $(SVF_ROOT)"
	@echo "Source files: $(words $(SOURCES))"
	@echo "Enhanced Features: Data structures, Function pointers, Read/Write separation"
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
	@echo "Checking enhanced libraries and headers..."
	@test -f $(SVF_ROOT)/lib/libSvfCore.a || (echo "❌ libSvfCore.a not found" && exit 1)
	@test -f $(SVF_ROOT)/lib/libSvfLLVM.a || (echo "❌ libSvfLLVM.a not found" && exit 1)
	@test -f $(SVF_ROOT)/include/SVFIR/SVFIR.h || (echo "❌ SVFIR.h not found" && exit 1)
	@test -f $(SVF_ROOT)/include/SVF-LLVM/LLVMModule.h || (echo "❌ LLVMModule.h not found" && exit 1)
	@echo "✅ Required libraries and headers found"
	@echo "✅ Enhanced analysis features enabled"
endif

# Build target with enhanced features
$(TARGET): $(OBJECTS)
	@echo "🔗 Linking $(TARGET) with enhanced features..."
	@echo "Enhanced features: Data structures, Function pointers, Memory operation analysis"
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "✅ Enhanced build completed: $(TARGET)"

# Compile rules with enhanced analysis
%.o: %.cpp
	@echo "🔨 Compiling $< (enhanced version)..."
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

# Enhanced test with sample data
test: $(TARGET) setup-svf
	@echo "🧪 Running enhanced analysis test..."
	@if [ -f "compile_commands.json" ] && [ -f "handler.json" ]; then \
		./$(TARGET) --compile-commands=compile_commands.json \
		            --handlers=handler.json \
		            --output=enhanced_results.json \
		            --verbose --detailed; \
		echo "✅ Enhanced test completed. Results in enhanced_results.json"; \
	else \
		echo "❌ Test requires compile_commands.json and handler.json"; \
		echo "Please provide these files to run the enhanced test"; \
	fi

# Quick test with basic output
test-quick: $(TARGET) setup-svf
	@echo "🧪 Running quick enhanced test..."
	@if [ -f "compile_commands.json" ] && [ -f "handler.json" ]; then \
		./$(TARGET) --compile-commands=compile_commands.json \
		            --handlers=handler.json \
		            --output=quick_results.json; \
		echo "✅ Quick test completed. Results in quick_results.json"; \
	else \
		echo "❌ Test requires compile_commands.json and handler.json"; \
	fi

# Debug build with enhanced features
debug: CXXFLAGS += -g -O0 -DDEBUG -DENHANCED_DEBUG
debug: clean $(TARGET)
	@echo "🐛 Enhanced debug build completed"

# Debug build with AddressSanitizer
debug-asan: CXXFLAGS += -g -O0 -DDEBUG -fsanitize=address -fno-omit-frame-pointer
debug-asan: LDFLAGS += -fsanitize=address
debug-asan: clean $(TARGET)
	@echo "🐛 Enhanced debug build with AddressSanitizer completed"

# Release build with enhanced optimizations
release: CXXFLAGS += -O3 -DNDEBUG -DENHANCED_RELEASE
release: clean $(TARGET)
	@echo "🚀 Enhanced release build completed"

# Enhanced dependency check
check-deps:
	@echo "🔍 Checking enhanced dependencies..."
	@which $(LLVM_CONFIG) > /dev/null || (echo "❌ llvm-config not found" && exit 1)
	@which $(CXX) > /dev/null || (echo "❌ clang++ not found" && exit 1)
	@echo "LLVM Version: $(shell $(LLVM_CONFIG) --version)"
	@echo "Compiler: $(shell $(CXX) --version | head -1)"
	@echo "SVF Status: $(SVF_STATUS)"
ifeq ($(SVF_AVAILABLE),1)
	@echo "Enhanced SVF Features: Enabled"
	@echo "SVF Include: $(SVF_ROOT)/include"
	@echo "SVF Lib: $(SVF_ROOT)/lib"
	@echo "✅ All enhanced dependencies OK"
else
	@echo "⚠️  Enhanced SVF features not available"
endif

# Show enhanced usage
help:
	@echo "Enhanced SVF Interrupt Handler Analyzer"
	@echo "======================================="
	@echo ""
	@echo "Enhanced Features:"
	@echo "  • Read/Write operation separation"
	@echo "  • Data structure field tracking"
	@echo "  • Function pointer resolution"
	@echo "  • Global/static variable modification detection"
	@echo "  • Detailed function call analysis"
	@echo ""
	@echo "Build Commands:"
	@echo "  make all          - Build the enhanced analyzer"
	@echo "  make clean        - Clean build files"
	@echo "  make debug        - Build enhanced debug version"
	@echo "  make debug-asan   - Build debug version with AddressSanitizer"
	@echo "  make release      - Build optimized enhanced version"
	@echo "  make test         - Run enhanced test with detailed output"
	@echo "  make test-quick   - Run quick enhanced test"
	@echo "  make install      - Install to /usr/local/bin"
	@echo "  make check-deps   - Check enhanced dependencies"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET) --compile-commands=<file> --handlers=<file> [options]"
	@echo ""
	@echo "Required:"
	@echo "  --compile-commands=<file>   compile_commands.json file"
	@echo "  --handlers=<file>           handler.json file"
	@echo ""
	@echo "Optional:"
	@echo "  --output=<file>             Output JSON file (default: enhanced_interrupt_analysis.json)"
	@echo "  --verbose                   Verbose output"
	@echo "  --detailed                  Detailed analysis output"
	@echo "  --help                      Show help"
	@echo ""
	@echo "Enhanced Analysis Output:"
	@echo "  • Separated read/write operations"
	@echo "  • Data structure access patterns"
	@echo "  • Function pointer target resolution"
	@echo "  • Global variable modification tracking"
	@echo "  • Detailed function call information"
	@echo ""
	@echo "Examples:"
	@echo "  ./$(TARGET) --compile-commands=cc.json --handlers=h.json"
	@echo "  ./$(TARGET) --compile-commands=cc.json --handlers=h.json --verbose --detailed"
	@echo "  ./$(TARGET) --compile-commands=cc.json --handlers=h.json --output=analysis.json"
	@echo ""
	@echo "Environment Variables:"
	@echo "  SVF_ROOT=<path>    Set SVF installation path (default: /opt/svf-llvm14)"

# Performance test
perf-test: $(TARGET) setup-svf
	@echo "📊 Running enhanced performance test..."
	@if [ -f "compile_commands.json" ] && [ -f "handler.json" ]; then \
		time ./$(TARGET) --compile-commands=compile_commands.json \
		                 --handlers=handler.json \
		                 --output=perf_results.json \
		                 --verbose; \
		echo "📊 Performance test completed"; \
		echo "Results saved to perf_results.json"; \
	else \
		echo "❌ Performance test requires compile_commands.json and handler.json"; \
	fi

# Memory usage test
memory-test: debug-asan setup-svf
	@echo "🧠 Running enhanced memory usage test..."
	@if [ -f "compile_commands.json" ] && [ -f "handler.json" ]; then \
		valgrind --tool=memcheck --leak-check=full \
		         ./$(TARGET) --compile-commands=compile_commands.json \
		                     --handlers=handler.json \
		                     --output=memory_test_results.json; \
		echo "🧠 Memory test completed"; \
	else \
		echo "❌ Memory test requires compile_commands.json and handler.json"; \
	fi

# Format code (requires clang-format)
format:
	@echo "🎨 Formatting enhanced code..."
	@find . -name "*.cpp" -o -name "*.h" | xargs clang-format -i -style=LLVM
	@echo "✅ Enhanced code formatted"

# Analyze code (requires clang-tidy)
analyze:
	@echo "🔍 Analyzing enhanced code..."
	@clang-tidy $(SOURCES) -- $(CXXFLAGS)
	@echo "✅ Enhanced code analysis completed"

# Count lines of code for enhanced version
loc:
	@echo "📊 Enhanced analyzer lines of code:"
	@wc -l $(SOURCES) *.h | tail -1

# Show enhanced compilation database
show-compile-db:
	@echo "📋 Enhanced compilation settings:"
	@echo "CXX: $(CXX)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "Sources: $(SOURCES)"
	@echo "Objects: $(OBJECTS)"
	@echo "Enhanced Features: ENHANCED_ANALYSIS, ENHANCED_SVF_FEATURES"

.PHONY: all info check-svf clean install uninstall test test-quick debug debug-asan release check-deps help format analyze loc show-compile-db perf-test memory-test

.DEFAULT_GOAL := all
