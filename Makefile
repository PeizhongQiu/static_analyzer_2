# Enhanced Cross-Module IRQ Analyzer - Simplified Makefile

# LLVM Configuration
LLVM_CONFIG = llvm-config
LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs all 2>/dev/null || $(LLVM_CONFIG) --libs core support analysis irreader)

# SVF Configuration (optional)
SVF_ROOT ?= /usr/local/svf
SVF_AVAILABLE := $(shell test -f $(SVF_ROOT)/include/SVF-LLVM/LLVMUtil.h && echo 1 || echo 0)

# Compiler and flags
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wno-unused-parameter $(LLVM_CXXFLAGS)
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS) -lpthread -ldl -lm

# SVF Integration
ifeq ($(SVF_AVAILABLE),1)
    CXXFLAGS += -DSVF_AVAILABLE -I$(SVF_ROOT)/include
    LDFLAGS += -L$(SVF_ROOT)/lib -lSvf
    SVF_STATUS = "Available"
else
    SVF_STATUS = "Not Available"
endif

# Targets
TARGET_ENHANCED = enhanced_irq_analyzer
TARGET_BASIC = irq_analyzer_cross_module

# Simplified Enhanced source files
ENHANCED_SOURCES = enhanced_main_updated.cpp \
                  EnhancedCrossModuleAnalyzer.cpp \
                  SVFAnalyzer.cpp \
                  SimpleEnhancedJSONOutput.cpp \
                  SimpleAnalysisComparator.cpp \
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

# Basic source files (fallback)
BASIC_SOURCES = main_cross_module.cpp \
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

ENHANCED_OBJECTS = $(ENHANCED_SOURCES:.cpp=.o)
BASIC_OBJECTS = $(BASIC_SOURCES:.cpp=.o)

# Default target
all: info auto

# Auto-detect and build
auto:
ifeq ($(SVF_AVAILABLE),1)
	@echo "SVF detected, building enhanced version..."
	$(MAKE) enhanced
else
	@echo "SVF not detected, building basic version..."
	$(MAKE) basic
endif

# Enhanced target
enhanced: $(TARGET_ENHANCED)

$(TARGET_ENHANCED): $(ENHANCED_OBJECTS)
	@echo "Linking enhanced analyzer..."
	$(CXX) $(ENHANCED_OBJECTS) -o $(TARGET_ENHANCED) $(LDFLAGS)
	@echo "✅ Enhanced build completed: $(TARGET_ENHANCED)"

# Basic target
basic: $(TARGET_BASIC)

$(TARGET_BASIC): $(BASIC_OBJECTS)
	@echo "Linking basic analyzer..."
	$(CXX) $(BASIC_OBJECTS) -o $(TARGET_BASIC) $(LDFLAGS)
	@echo "✅ Basic build completed: $(TARGET_BASIC)"

# Compile rules
enhanced_main_updated.o: enhanced_main_updated.cpp
	@echo "Compiling enhanced main..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

SimpleEnhancedJSONOutput.o: SimpleEnhancedJSONOutput.cpp
	@echo "Compiling simple enhanced JSON output..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

SimpleAnalysisComparator.o: SimpleAnalysisComparator.cpp
	@echo "Compiling simple analysis comparator..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean targets
clean:
	rm -f $(ENHANCED_OBJECTS) $(BASIC_OBJECTS) $(TARGET_ENHANCED) $(TARGET_BASIC)
	@echo "Clean completed"

# Test targets
test-enhanced: $(TARGET_ENHANCED)
	@echo "Testing enhanced analyzer..."
	@mkdir -p test
	@echo '{"total_unique_combinations": 2, "combinations": [{"handler": "test_handler"}, {"handler": "acpi_handler"}]}' > test/handler.json
	@echo '[{"directory": ".", "command": "clang -c test.c", "file": "test.c"}]' > test/compile_commands.json
	./$(TARGET_ENHANCED) --compile-commands=test/compile_commands.json \
	                     --handlers=test/handler.json \
	                     --output=test/enhanced_results.json \
	                     --generate-reports --verbose || true

test-basic: $(TARGET_BASIC)
	@echo "Testing basic analyzer..."
	@mkdir -p test
	@echo '{"total_unique_combinations": 2, "combinations": [{"handler": "test_handler"}, {"handler": "acpi_handler"}]}' > test/handler.json
	@echo '[{"directory": ".", "command": "clang -c test.c", "file": "test.c"}]' > test/compile_commands.json
	./$(TARGET_BASIC) --compile-commands=test/compile_commands.json \
	                  --handlers=test/handler.json \
	                  --output=test/basic_results.json --verbose || true

test: auto
ifeq ($(SVF_AVAILABLE),1)
	$(MAKE) test-enhanced
else
	$(MAKE) test-basic
endif

# Comparison test
test-comparison: $(TARGET_ENHANCED)
	@echo "Running comparison test..."
	@mkdir -p test
	./$(TARGET_ENHANCED) --compile-commands=test/compile_commands.json \
	                     --handlers=test/handler.json \
	                     --output=test/comparison_results.json \
	                     --compare-with-basic \
	                     --generate-reports \
	                     --verbose || true

# Check dependencies
check:
	@echo "Enhanced IRQ Analyzer - Simplified Build"
	@echo "========================================"
	@which $(LLVM_CONFIG) > /dev/null || (echo "Error: llvm-config not found" && exit 1)
	@which $(CXX) > /dev/null || (echo "Error: clang++ not found" && exit 1)
	@echo "LLVM Version: $(shell $(LLVM_CONFIG) --version)"
	@echo "SVF Status: $(SVF_STATUS)"
	@echo "SVF Available: $(SVF_AVAILABLE)"
ifeq ($(SVF_AVAILABLE),1)
	@echo "✅ SVF integration enabled"
else
	@echo "⚠️ SVF integration disabled"
endif
	@echo "Dependencies OK"

# Show build info
info:
	@echo "Enhanced Cross-Module IRQ Analyzer (Simplified)"
	@echo "=============================================="
	@echo "Targets:"
	@echo "  auto        - Auto-detect and build best version"
	@echo "  enhanced    - Build with SVF integration"
	@echo "  basic       - Build without SVF"
	@echo "  test        - Run tests"
	@echo "  clean       - Clean build files"
	@echo "  check       - Check dependencies"
	@echo ""
	@echo "Configuration:"
	@echo "  Enhanced Target: $(TARGET_ENHANCED)"
	@echo "  Basic Target: $(TARGET_BASIC)"
	@echo "  LLVM Version: $(shell $(LLVM_CONFIG) --version)"
	@echo "  SVF Status: $(SVF_STATUS)"
	@echo "  Sources: $(words $(ENHANCED_SOURCES)) files (simplified)"

# Help
help: info
	@echo ""
	@echo "Usage Examples:"
	@echo "  make auto                    # Build best available version"
	@echo "  make test                    # Run tests"
	@echo "  make test-comparison         # Run comparison test"
	@echo ""
	@echo "Running the analyzer:"
	@echo "  ./$(TARGET_ENHANCED) --compile-commands=cc.json --handlers=h.json"
	@echo "  ./$(TARGET_ENHANCED) --compile-commands=cc.json --handlers=h.json --generate-reports"
	@echo "  ./$(TARGET_ENHANCED) --compile-commands=cc.json --handlers=h.json --compare-with-basic"

# Debug build
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: clean enhanced
	@echo "Debug build completed"

# Release build  
release: CXXFLAGS += -O3 -DNDEBUG
release: clean enhanced
	@echo "Release build completed"

# Install
PREFIX ?= /usr/local
install: enhanced
	@echo "Installing to $(PREFIX)/bin/..."
	install -d $(PREFIX)/bin
	install -m 755 $(TARGET_ENHANCED) $(PREFIX)/bin/
	@echo "Installation completed"

.PHONY: all auto enhanced basic clean test test-enhanced test-basic test-comparison check info help debug release install

.DEFAULT_GOAL := auto
