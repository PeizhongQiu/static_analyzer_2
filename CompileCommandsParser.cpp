//===- CompileCommandsParser.cpp - Compile Commands Parser Implementation ===//

#include "CompileCommandsParser.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <fstream>

using namespace llvm;

bool CompileCommandsParser::parseFromFile(const std::string& filepath) {
    outs() << "📋 Parsing compile_commands.json: " << filepath << "\n";
    
    // 检查文件是否存在
    if (!sys::fs::exists(filepath)) {
        errs() << "❌ File does not exist: " << filepath << "\n";
        return false;
    }
    
    // 读取文件
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr = 
        MemoryBuffer::getFile(filepath);
    
    if (std::error_code EC = BufferOrErr.getError()) {
        errs() << "❌ Error reading compile_commands.json: " << EC.message() << "\n";
        return false;
    }
    
    // 解析JSON
    Expected<json::Value> JsonOrErr = json::parse(BufferOrErr.get()->getBuffer());
    if (!JsonOrErr) {
        errs() << "❌ Error parsing JSON: " << toString(JsonOrErr.takeError()) << "\n";
        return false;
    }
    
    json::Value &Json = *JsonOrErr;
    const json::Array *CommandsArray = Json.getAsArray();
    if (!CommandsArray) {
        errs() << "❌ Expected JSON array in compile_commands.json\n";
        return false;
    }
    
    outs() << "✅ Found " << CommandsArray->size() << " compile commands\n";
    
    // 清空现有命令
    commands.clear();
    
    size_t parsed = 0;
    size_t skipped = 0;
    
    // 解析每个编译命令
    for (const json::Value &Cmd : *CommandsArray) {
        const json::Object *CmdObj = Cmd.getAsObject();
        if (!CmdObj) {
            skipped++;
            continue;
        }
        
        CompileCommand command;
        
        // 解析directory字段
        if (auto Dir = CmdObj->getString("directory")) {
            command.directory = std::string(*Dir);
        }
        
        // 解析file字段
        if (auto File = CmdObj->getString("file")) {
            command.file = std::string(*File);
        }
        
        // 解析command字段
        if (auto Command = CmdObj->getString("command")) {
            command.command = std::string(*Command);
        }
        
        // 解析arguments字段（可选）
        if (auto Args = CmdObj->getArray("arguments")) {
            for (const json::Value &Arg : *Args) {
                if (auto ArgStr = Arg.getAsString()) {
                    command.arguments.push_back(std::string(*ArgStr));
                }
            }
        }
        
        // 验证必需字段
        if (command.directory.empty() || command.file.empty()) {
            skipped++;
            continue;
        }
        
        // 只处理C/C++源文件
        std::string file_ext = sys::path::extension(command.file).str();
        if (file_ext != ".c" && file_ext != ".cpp" && file_ext != ".cc" && file_ext != ".cxx") {
            skipped++;
            continue;
        }
        
        commands.push_back(command);
        parsed++;
    }
    
    outs() << "📊 Parsing summary:\n";
    outs() << "  ✅ Parsed: " << parsed << " commands\n";
    outs() << "  ⏭️  Skipped: " << skipped << " commands\n";
    
    if (parsed == 0) {
        errs() << "❌ No valid compile commands found\n";
        return false;
    }
    
    return true;
}

std::vector<std::string> CompileCommandsParser::getBitcodeFiles() const {
    std::vector<std::string> bitcode_files;
    
    if (commands.empty()) {
        return bitcode_files;
    }
    
    outs() << "🔍 Converting source files to bitcode paths...\n";
    
    size_t converted = 0;
    size_t missing = 0;
    
    for (const auto& cmd : commands) {
        // 构建完整的源文件路径
        std::string source_file = cmd.file;
        
        // 如果是相对路径，则相对于directory
        if (!sys::path::is_absolute(source_file)) {
            SmallString<256> full_path(cmd.directory);
            sys::path::append(full_path, source_file);
            source_file = full_path.str().str();
        }
        
        // 将源文件扩展名替换为.bc
        std::string bc_file = source_file;
        size_t dot_pos = bc_file.find_last_of('.');
        if (dot_pos != std::string::npos) {
            bc_file = bc_file.substr(0, dot_pos) + ".bc";
            
            // 检查.bc文件是否存在
            if (sys::fs::exists(bc_file)) {
                bitcode_files.push_back(bc_file);
                converted++;
            } else {
                missing++;
                // 可选：显示前几个缺失的文件用于调试
                if (missing <= 5) {
                    outs() << "  ⚠️  Missing: " << bc_file << "\n";
                }
            }
        }
    }
    
    outs() << "📊 Bitcode file conversion summary:\n";
    outs() << "  ✅ Found: " << converted << " .bc files\n";
    outs() << "  ❌ Missing: " << missing << " .bc files\n";
    
    if (missing > 5) {
        outs() << "  (Only first 5 missing files shown)\n";
    }
    
    if (converted == 0) {
        outs() << "⚠️  No .bc files found. You may need to:\n";
        outs() << "    1. Compile with clang to generate .bc files\n";
        outs() << "    2. Use -emit-llvm flag during compilation\n";
        outs() << "    3. Check if .bc files are in the expected locations\n";
    }
    
    return bitcode_files;
}
