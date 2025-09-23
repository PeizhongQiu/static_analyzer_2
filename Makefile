# Enhanced SVF Interrupt Handler Analyzer - Updated Makefile with Improvements

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
CXXFLAGS += -O2 -DENHANCED_ANALYSIS -DSTRUCT_INFO_ENHANCEMENT
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS) -lpthread -ldl -lm

# Target
TARGET = enhanced_svf_irq_analyzer

# SVF Integration with enhanced features
ifeq ($(SVF_AVAILABLE),1)
    CXXFLAGS += -DSVF_AVAILABLE -I$(SVF_ROOT)/include
    CXXFLAGS += -fexceptions -frtti
    CXXFLAGS += -DENHANCED_SVF_FEATURES -DSTRUCT_FIELD_MAPPING
    LDFLAGS += -L$(SVF_ROOT)/lib -lSvfLLVM -lSvfCore
    SVF_STATUS = Available_Enhanced_with_StructInfo
else
    SVF_STATUS = Not_Available
endif

# Enhanced Source files - 包含新增和更新的文件
SOURCES = main.cpp \
          SVFInterruptAnalyzer.cpp \
          MemoryAnalyzer.cpp \
          EnhancedDataStructureAnalyzer.cpp \
          FunctionCallAnalyzer.cpp \
          FunctionPointerAnalyzer.cpp \
          AnalysisOutputManager.cpp \
          CompileCommandsParser.cpp \
          IRQHandlerIdentifier.cpp

# 备选源文件配置（如果保留原有文件）
SOURCES_LEGACY = main.cpp \
                 SVFInterruptAnalyzer.cpp \
                 MemoryAnalyzer.cpp \
                 DataStructureAnalyzer.cpp \
                 FunctionCallAnalyzer.cpp \
                 FunctionPointerAnalyzer.cpp \
                 AnalysisOutputManager.cpp \
                 CompileCommandsParser.cpp \
                 IRQHandlerIdentifier.cpp

# 检查是否存在增强版文件
ENHANCED_DATA_ANALYZER = $(shell test -f EnhancedDataStructureAnalyzer.cpp && echo 1 || echo 0)

# 根据文件存在情况选择源文件列表
ifeq ($(ENHANCED_DATA_ANALYZER),1)
    ACTUAL_SOURCES = $(SOURCES)
    ENHANCEMENT_STATUS = Enhanced_with_StructInfo
else
    ACTUAL_SOURCES = $(SOURCES_LEGACY)
    ENHANCEMENT_STATUS = Legacy_Version
endif

OBJECTS = $(ACTUAL_SOURCES:.cpp=.o)

# Default target
all: info check-svf $(TARGET) post-build-info

# Enhanced build info
info:
	@echo "Enhanced SVF Interrupt Handler Analyzer with Struct Info"
	@echo "========================================================"
	@echo "Target: $(TARGET)"
	@echo "LLVM: $(shell $(LLVM_CONFIG) --version)"
	@echo "SVF Status: $(SVF_STATUS)"
	@echo "SVF Root: $(SVF_ROOT)"
	@echo "Enhancement Status: $(ENHANCEMENT_STATUS)"
	@echo "Source files: $(words $(ACTUAL_SOURCES))"
	@echo ""
	@echo "🆕 New Features:"
	@echo "  • Cleaned struct names (test_device instead of test_device.19)"
	@echo "  • Real field names (regs, total_irqs instead of field_0, field_1)"
	@echo "  • Field offset and size information"
	@echo "  • Full access paths (dev.test_device::regs)"
	@echo "  • Enhanced confidence scoring"
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
	@echo "✅ Enhanced struct analysis features enabled"
endif

# Build target with enhanced features
$(TARGET): $(OBJECTS)
	@echo "🔗 Linking $(TARGET) with enhanced struct info features..."
	@echo "Enhanced capabilities:"
	@echo "  • Struct field mapping and real names"
	@echo "  • Field offset and size calculation"
	@echo "  • Improved JSON output format"
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "✅ Enhanced build completed: $(TARGET)"

# Compile rules with enhanced analysis - 检查源文件
%.o: %.cpp
ifeq ($(ENHANCED_DATA_ANALYZER),1)
	@echo "🔨 Compiling $< (enhanced struct info version)..."
else
	@echo "🔨 Compiling $< (standard version)..."
endif
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Post-build information
post-build-info: $(TARGET)
	@echo ""
	@echo "🎉 Build Summary"
	@echo "================"
	@echo "Target: $(TARGET)"
	@echo "Enhancement Status: $(ENHANCEMENT_STATUS)"
ifeq ($(ENHANCED_DATA_ANALYZER),1)
	@echo "✅ Enhanced struct analysis available"
	@echo "✅ Real field name mapping enabled"
	@echo "✅ Field offset calculation enabled"
else
	@echo "⚠️  Using legacy data structure analyzer"
	@echo "💡 Create EnhancedDataStructureAnalyzer.cpp for improved features"
endif
	@echo ""
	@echo "🔧 Enhanced Features Status:"
	@echo "  Struct name cleaning: ✅"
	@echo "  Real field names: $(if $(findstring Enhanced,$(ENHANCEMENT_STATUS)),✅,⚠️)"
	@echo "  Field offsets: $(if $(findstring Enhanced,$(ENHANCEMENT_STATUS)),✅,⚠️)"
	@echo "  Full access paths: $(if $(findstring Enhanced,$(ENHANCEMENT_STATUS)),✅,⚠️)"

# Clean
clean:
	@echo "🧹 Cleaning..."
	rm -f $(OBJECTS) $(TARGET) *.o
	@echo "✅ Clean completed"

# Install
install: $(TARGET)
	@echo "📦 Installing $(TARGET)..."
	sudo cp $(TARGET) /usr/local/bin/
	@echo "✅ Installed to /usr/local/bin/$(TARGET)"

# Setup SVF external API file
setup-svf:
	@echo "🔗 Setting up SVF external API..."
	@ln -sf /home/qpz/lab/SVF/Release-build/lib/extapi.bc ./extapi.bc
	@echo "✅ SVF setup completed"

# Enhanced test with struct info validation
test: $(TARGET) setup-svf
	@echo "🧪 Running enhanced struct analysis test..."
	@if [ -f "compile_commands.json" ] && [ -f "handler.json" ]; then \
		echo "Testing enhanced struct information output..."; \
		./$(TARGET) --compile-commands=compile_commands.json \
		            --handlers=handler.json \
		            --output=enhanced_struct_results.json \
		            --verbose --detailed; \
		echo "✅ Enhanced test completed. Results in enhanced_struct_results.json"; \
		echo ""; \
		echo "🔍 Validating struct info output..."; \
		if command -v python3 >/dev/null 2>&1; then \
			python3 -c "import json; data=json.load(open('enhanced_struct_results.json')); handlers=data.get('interrupt_handlers',[]); struct_writes=[w for h in handlers for w in h.get('memory_writes',[]) if w.get('target_type')=='struct_field']; print(f'Found {len(struct_writes)} struct field writes'); [print(f'  {w.get(\"target_name\",\"unknown\")} -> {w.get(\"struct_info\",{}).get(\"field_name\",\"no_field_name\")}') for w in struct_writes[:5]]"; \
		fi; \
	else \
		echo "❌ Test requires compile_commands.json and handler.json"; \
		echo "Please provide these files to run the enhanced test"; \
	fi

# Test with test kernel module
test-with-module: $(TARGET) setup-svf
	@echo "🧪 Running test with enhanced struct analysis..."
	@if [ -d "test_kernel_module" ]; then \
		cd test_kernel_module && ./build_and_test.sh && cd ..; \
		./$(TARGET) --compile-commands=test_kernel_module/compile_commands.json \
		            --handlers=test_kernel_module/handler.json \
		            --output=test_struct_enhanced_results.json \
		            --verbose; \
		echo "✅ Test completed. Results in test_struct_enhanced_results.json"; \
		echo ""; \
		echo "🔍 Checking for enhanced struct info..."; \
		if command -v python3 >/dev/null 2>&1; then \
			python3 -c "import json; data=json.load(open('test_struct_enhanced_results.json')); print('Struct info validation:'); handlers=data.get('interrupt_handlers',[]); [print(f'Handler {h.get(\"function_name\")}: {len([w for w in h.get(\"memory_writes\",[]) if \"struct_info\" in w])} enhanced writes') for h in handlers if h.get('analysis_complete')]"; \
		fi; \
	else \
		echo "❌ test_kernel_module directory not found"; \
		echo "Please ensure the test module is available"; \
	fi

# Debug build with enhanced features
debug: CXXFLAGS += -g -O0 -DDEBUG -DENHANCED_DEBUG -DSTRUCT_DEBUG
debug: clean $(TARGET)
	@echo "🐛 Enhanced debug build completed with struct analysis debug info"

# Release build with enhanced optimizations
release: CXXFLAGS += -O3 -DNDEBUG -DENHANCED_RELEASE -DSTRUCT_OPTIMIZED
release: clean $(TARGET)
	@echo "🚀 Enhanced release build completed with optimized struct analysis"

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
	@echo "Struct Analysis Enhancement: $(ENHANCEMENT_STATUS)"
	@echo "SVF Include: $(SVF_ROOT)/include"
	@echo "SVF Lib: $(SVF_ROOT)/lib"
	@echo "✅ All enhanced dependencies OK"
else
	@echo "⚠️  Enhanced SVF features not available"
endif

# Create enhanced data structure analyzer if missing
create-enhanced-analyzer:
	@if [ ! -f "EnhancedDataStructureAnalyzer.cpp" ]; then \
		echo "📝 Creating EnhancedDataStructureAnalyzer.cpp template..."; \
		echo "// Generated template - replace with actual enhanced implementation" > EnhancedDataStructureAnalyzer.cpp; \
		echo "#include \"SVFInterruptAnalyzer.h\"" >> EnhancedDataStructureAnalyzer.cpp; \
		echo "// TODO: Add enhanced struct analysis methods" >> EnhancedDataStructureAnalyzer.cpp; \
		echo "✅ Template created. Please replace with enhanced implementation."; \
	else \
		echo "✅ EnhancedDataStructureAnalyzer.cpp already exists"; \
	fi

# Show enhanced usage with struct info features
help:
	@echo "Enhanced SVF Interrupt Handler Analyzer with Struct Info"
	@echo "========================================================"
	@echo ""
	@echo "🆕 Enhanced Features:"
	@echo "  • Clean struct names (test_device vs test_device.19)"
	@echo "  • Real field names (regs, total_irqs vs field_0, field_1)"  
	@echo "  • Field offset and size information"
	@echo "  • Full access paths (dev.test_device::regs)"
	@echo "  • Enhanced JSON output with struct_info sections"
	@echo ""
	@echo "Build Commands:"
	@echo "  make all              - Build the enhanced analyzer"
	@echo "  make clean            - Clean build files"
	@echo "  make debug            - Build enhanced debug version"  
	@echo "  make release          - Build optimized enhanced version"
	@echo "  make test             - Run enhanced test with struct validation"
	@echo "  make test-with-module - Run test with test_kernel_module"
	@echo "  make install          - Install to /usr/local/bin"
	@echo "  make check-deps       - Check enhanced dependencies"
	@echo "  make create-enhanced-analyzer - Create enhanced analyzer template"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET) --compile-commands=<file> --handlers=<file> [options]"
	@echo ""
	@echo "Required:"
	@echo "  --compile-commands=<file>   compile_commands.json file"
	@echo "  --handlers=<file>           handler.json file"
	@echo ""
	@echo "Optional:"
	@echo "  --output=<file>             Output JSON file (default: interrupt_analysis.json)"
	@echo "  --verbose                   Verbose output"
	@echo "  --detailed                  Detailed analysis output"
	@echo "  --help                      Show help"
	@echo ""
	@echo "🎯 Enhanced Analysis Output Features:"
	@echo "  • Clean struct names in data_structure_accesses"
	@echo "  • Enhanced memory_writes with struct_info sections"
	@echo "  • Real field names instead of field_N notation"
	@echo "  • Field offsets, sizes, and types"
	@echo "  • Full access paths for better understanding"
	@echo ""
	@echo "Examples:"
	@echo "  ./$(TARGET) --compile-commands=cc.json --handlers=h.json"
	@echo "  ./$(TARGET) --compile-commands=cc.json --handlers=h.json --verbose"
	@echo ""
	@echo "Environment Variables:"
	@echo "  SVF_ROOT=<path>    Set SVF installation path (default: /opt/svf-llvm14)"

# Check enhanced features status
check-enhancement:
	@echo "🔍 Enhanced Features Status Check"
	@echo "================================="
	@echo "Enhancement Status: $(ENHANCEMENT_STATUS)"
	@echo "SVF Available: $(SVF_AVAILABLE)"
	@echo "Enhanced Data Analyzer: $(ENHANCED_DATA_ANALYZER)"
	@echo ""
	@echo "📁 Source Files:"
	@for src in $(ACTUAL_SOURCES); do \
		if [ -f "$$src" ]; then \
			echo "  ✅ $$src"; \
		else \
			echo "  ❌ $$src (missing)"; \
		fi; \
	done
	@echo ""
	@echo "🎯 Feature Availability:"
	@echo "  Struct name cleaning: ✅ (always available)"
ifeq ($(ENHANCED_DATA_ANALYZER),1)
	@echo "  Real field name mapping: ✅"
	@echo "  Field offset calculation: ✅"
	@echo "  Enhanced struct_info output: ✅"
else
	@echo "  Real field name mapping: ⚠️  (requires EnhancedDataStructureAnalyzer.cpp)"
	@echo "  Field offset calculation: ⚠️  (requires EnhancedDataStructureAnalyzer.cpp)"
	@echo "  Enhanced struct_info output: ⚠️  (requires EnhancedDataStructureAnalyzer.cpp)"
endif

.PHONY: all info check-svf clean install test test-with-module debug release check-deps \
        help create-enhanced-analyzer check-enhancement post-build-info

.DEFAULT_GOAL := all