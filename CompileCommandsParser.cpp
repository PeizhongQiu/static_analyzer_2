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
    json::Array *CommandsArray = Json.getAsArray();
    if (!CommandsArray) {
        errs() << "Expected JSON array in compile_commands.json\n";
        return false;
    }
    
    for (const json::Value &Cmd : *CommandsArray) {
        json::Object *CmdObj = Cmd.getAsObject();
        if (!CmdObj) continue;
        
        CompileCommand command;
        if (auto Dir = CmdObj->getString("directory"))
            command.directory = *Dir;
        if (auto File = CmdObj->getString("file"))
            command.file = *File;
        if (auto Command = CmdObj->getString("command"))
            command.command = *Command;
        
        // 解析参数
        if (auto Args = CmdObj->getArray("arguments")) {
            for (const json::Value &Arg : *Args) {
                if (auto ArgStr = Arg.getAsString())
                    command.arguments.push_back(*ArgStr);
            }
        }
        
        commands.push_back(command);
    }
    
    return true;
}

std::vector<std::string> CompileCommandsParser::getBitcodeFiles() const {
    std::vector<std::string> bitcode_files;
    
    for (const auto& cmd : commands) {
        // 查找 .bc 或 .ll 文件
        if (cmd.file.ends_with(".c") || cmd.file.ends_with(".cpp")) {
            std::string bc_file = cmd.file;
            bc_file.replace(bc_file.find_last_of('.'), 4, ".bc");
            bitcode_files.push_back(bc_file);
        }
    }
    
    return bitcode_files;
}
