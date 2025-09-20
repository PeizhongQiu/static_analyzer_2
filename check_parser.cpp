#include "CompileCommandsParser.h"
#include <iostream>

int main() {
    CompileCommandsParser parser;
    if (parser.parseFromFile("../kafl.linux/compile_commands.json")) {
        auto bc_files = parser.getBitcodeFiles();
        std::cout << "CompileCommandsParser expects " << bc_files.size() << " .bc files" << std::endl;
        
        std::cout << "First 10 expected files:" << std::endl;
        for (size_t i = 0; i < std::min(bc_files.size(), size_t(10)); ++i) {
            std::cout << "  " << bc_files[i] << std::endl;
        }
        
        // 检查是否包含AER相关的文件
        std::cout << "\nLooking for AER-related files in expected list..." << std::endl;
        for (const auto& file : bc_files) {
            if (file.find("aer") != std::string::npos || 
                file.find("pci") != std::string::npos) {
                std::cout << "  📁 " << file << std::endl;
            }
        }
    }
    return 0;
}
