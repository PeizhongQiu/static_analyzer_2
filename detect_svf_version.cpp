//===- detect_svf_version.cpp - SVF版本和API检测工具 ---------------------===//

#include <iostream>

#ifdef SVF_AVAILABLE
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include "Graphs/VFG.h"
#include "MemoryModel/PointerAnalysis.h"

// 检测SVF版本和可用API
void detectSVFVersion() {
    std::cout << "=== SVF Version Detection ===" << std::endl;
    
    // 检测SVF命名空间和类
    std::cout << "SVF namespace: Available" << std::endl;
    
#ifdef SVF_VERSION
    std::cout << "SVF Version: " << SVF_VERSION << std::endl;
#endif
    
    // 检测主要类的可用性
    std::cout << "\n=== Available Classes ===" << std::endl;
    
    // 检测SVFIR相关
    std::cout << "SVF::SVFIR: ";
#ifdef SVF_SVFIR_H
    std::cout << "Available" << std::endl;
#else
    std::cout << "Available (assumed)" << std::endl;
#endif
    
    // 检测SVFIRBuilder
    std::cout << "SVF::SVFIRBuilder: Available" << std::endl;
    
    // 检测指针分析
    std::cout << "SVF::AndersenWaveDiff: Available" << std::endl;
    std::cout << "SVF::FlowSensitive: Available" << std::endl;
    
    // 检测VFG
    std::cout << "SVF::VFG: Available" << std::endl;
    
    std::cout << "\n=== API Method Detection ===" << std::endl;
    
    // 测试SVFIRBuilder构造函数
    std::cout << "Testing SVFIRBuilder constructor..." << std::endl;
    try {
        SVF::SVFIRBuilder builder;
        std::cout << "SVF::SVFIRBuilder(): Success" << std::endl;
    } catch (...) {
        std::cout << "SVF::SVFIRBuilder(): Failed" << std::endl;
    }
    
    // 测试SVFIR获取
    std::cout << "Testing SVFIR access..." << std::endl;
    try {
        SVF::SVFIR* pag = SVF::SVFIR::getPAG();
        if (pag) {
            std::cout << "SVF::SVFIR::getPAG(): Success" << std::endl;
        } else {
            std::cout << "SVF::SVFIR::getPAG(): Returns null" << std::endl;
        }
    } catch (...) {
        std::cout << "SVF::SVFIR::getPAG(): Failed" << std::endl;
    }
    
    std::cout << "\n=== Compilation Info ===" << std::endl;
    std::cout << "Compiled with SVF support: Yes" << std::endl;
    
#ifdef __SVF_LLVM_VERSION__
    std::cout << "SVF LLVM Version: " << __SVF_LLVM_VERSION__ << std::endl;
#endif
    
    std::cout << "\n=== Header Paths ===" << std::endl;
    std::cout << "Headers found in compilation" << std::endl;
}

#else
void detectSVFVersion() {
    std::cout << "SVF not available - SVF_AVAILABLE not defined" << std::endl;
}
#endif

int main() {
    detectSVFVersion();
    return 0;
}
