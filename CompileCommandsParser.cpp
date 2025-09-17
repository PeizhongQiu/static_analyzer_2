//===- CompileCommandsParser.cpp - Compile Commands Parser Implementation ===//

#include "CompileCommandsParser.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

using namespace llvm;

bool CompileCommandsParser::parseFromFile(const std::string& filepath) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr = 
        MemoryBuffer::getFile(filepath);
    
    if (std::error_code EC = BufferOrErr.getError()) {
        errs() << "Error reading compile_commands.json: " << EC.message() << "\n";
        return false;
    }
    
    Expected<json::Value> JsonOrErr = json::parse(BufferOrErr.get()->getBuffer());
    if (!JsonOrErr) {
        errs() << "Error parsing JSON: " << toString(JsonOrErr.takeError()) << "\n";
        return false;
    }
    
    json::Value &Json = *JsonOrErr;
    const json::Array *CommandsArray = Json.getAsArray();
    if (!CommandsArray) {
        errs() << "Expected JSON array in compile_commands.json\n";
        return false;
    }
    
    for (const json::Value &Cmd : *CommandsArray) {
        const json::Object *CmdObj = Cmd.getAsObject();
        if (!CmdObj) continue;
        
        CompileCommand command;
        if (auto Dir = CmdObj->getString("directory"))
            command.directory = std::string(*Dir);
        if (auto File = CmdObj->getString("file"))
            command.file = std::string(*File);
        if (auto Command = CmdObj->getString("command"))
            command.command = std::string(*Command);
        
        // 解析参数
        if (auto Args = CmdObj->getArray("arguments")) {
            for (const json::Value &Arg : *Args) {
                if (auto ArgStr = Arg.getAsString())
                    command.arguments.push_back(std::string(*ArgStr));
            }
        }
        
        commands.push_back(command);
    }
    
    return true;
}

std::vector<std::string> CompileCommandsParser::getBitcodeFiles() const {
    std::vector<std::string> bitcode_files;
    
    for (const auto& cmd : commands) {
        // 查找 .bc 或 .ll 文件 - 使用兼容的字符串方法
        if ((cmd.file.size() > 2 && cmd.file.substr(cmd.file.size()-2) == ".c") ||
            (cmd.file.size() > 4 && cmd.file.substr(cmd.file.size()-4) == ".cpp")) {
            std::string bc_file = cmd.file;
            size_t dot_pos = bc_file.find_last_of('.');
            if (dot_pos != std::string::npos) {
                bc_file = bc_file.substr(0, dot_pos) + ".bc";
                bitcode_files.push_back(bc_file);
            }
        }
    }
    
    return bitcode_files;
}
