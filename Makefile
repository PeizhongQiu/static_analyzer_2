# SVF Interrupt Handler Analyzer - Fixed Syntax

# LLVM Configuration
LLVM_CONFIG = llvm-config
LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs core support analysis irreader bitreader)

# SVF Configuration
SVF_ROOT ?= /opt/svf-llvm14
SVF_AVAILABLE := $(shell test -f $(SVF_ROOT)/include/SVF-LLVM/LLVMUtil.h && echo 1 || echo 0)

# Compiler and flags
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wno-unused-parameter $(LLVM_CXXFLAGS)
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS) -lpthread -ldl -lm

# Target
TARGET = svf_irq_analyzer

# SVF Integration - Fixed library linking
ifeq ($(SVF_AVAILABLE),1)
    CXXFLAGS += -DSVF_AVAILABLE -I$(SVF_ROOT)/include
    CXXFLAGS += -fexceptions -frtti
    LDFLAGS += -L$(SVF_ROOT)/lib -lSvfLLVM -lSvfCore
    SVF_STATUS = Available_with_SvfCore_and_SvfLLVM
else
    SVF_STATUS = Not_Available
endif

# Source files
SOURCES = main.cpp \
          SVFAnalyzer.cpp \
          SVFJSONOutput.cpp \
          CompileCommandsParser.cpp \
          IRQHandlerIdentifier.cpp

OBJECTS = $(SOURCES:.cpp=.o)

# Default target
all: info check-svf $(TARGET)

# Build info - Fixed syntax
info:
	@echo "SVF Interrupt Handler Analyzer"
	@echo "=============================="
	@echo "Target: $(TARGET)"
	@echo "LLVM: $(shell $(LLVM_CONFIG) --version)"
	@echo "SVF Status: $(SVF_STATUS)"
	@echo "SVF Root: $(SVF_ROOT)"
	@echo "Files: $(words $(SOURCES)) source files"
	@echo ""

# Check SVF availability
check-svf:
ifeq ($(SVF_AVAILABLE),0)
	@echo "Error: SVF not found at $(SVF_ROOT)"
	@echo "Please install SVF or set SVF_ROOT environment variable"
	@exit 1
else
	@echo "SVF found at $(SVF_ROOT)"
	@echo "Checking libraries..."
	@test -f $(SVF_ROOT)/lib/libSvfCore.a || (echo "libSvfCore.a not found" && exit 1)
	@test -f $(SVF_ROOT)/lib/libSvfLLVM.a || (echo "libSvfLLVM.a not found" && exit 1)
	@echo "Libraries OK: libSvfCore.a + libSvfLLVM.a"
endif

# Build target
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build completed: $(TARGET)"

# Compile rules
%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	@echo "Cleaning..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean completed"

# Test
test: $(TARGET)
	@echo "Testing $(TARGET)..."
	@if [ -f "compile_commands.json" ] && [ -f "handler.json" ]; then \
		./$(TARGET) --compile-commands=compile_commands.json \
		            --handlers=handler.json \
		            --output=test_results.json \
		            --verbose; \
	else \
		echo "Test requires compile_commands.json and handler.json"; \
	fi

# Debug build
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: clean $(TARGET)
	@echo "Debug build completed"

# Release build
release: CXXFLAGS += -O3 -DNDEBUG
release: clean $(TARGET)
	@echo "Release build completed"

# Check dependencies
check:
	@echo "Checking dependencies..."
	@which $(LLVM_CONFIG) > /dev/null || (echo "llvm-config not found" && exit 1)
	@which $(CXX) > /dev/null || (echo "clang++ not found" && exit 1)
	@echo "LLVM Version: $(shell $(LLVM_CONFIG) --version)"
	@echo "SVF Status: $(SVF_STATUS)"
ifeq ($(SVF_AVAILABLE),1)
	@echo "All dependencies OK"
else
	@echo "SVF missing"
	@exit 1
endif

# Help
help:
	@echo "SVF Interrupt Handler Analyzer"
	@echo "=============================="
	@echo "Build Commands:"
	@echo "  make all     - Build analyzer"
	@echo "  make clean   - Clean build files"
	@echo "  make debug   - Build debug version"
	@echo "  make release - Build optimized version"
	@echo "  make check   - Check dependencies"
	@echo "  make test    - Run test"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET) --compile-commands=cc.json --handlers=h.json"

.PHONY: all info check-svf clean test debug release check help

.DEFAULT_GOAL := all
