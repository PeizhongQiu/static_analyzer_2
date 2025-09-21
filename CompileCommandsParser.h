//===- CompileCommandsParser.h - Compile Commands Parser ----------------===//
//
// 解析 compile_commands.json 文件的工具类
//
//===----------------------------------------------------------------------===//

#ifndef COMPILE_COMMANDS_PARSER_H
#define COMPILE_COMMANDS_PARSER_H

#include <string>
#include <vector>

//===----------------------------------------------------------------------===//
// 编译命令解析器
//===----------------------------------------------------------------------===//

class CompileCommandsParser {
private:
    struct CompileCommand {
        std::string directory;
        std::string file;
        std::string command;
        std::vector<std::string> arguments;
    };
    
    std::vector<CompileCommand> commands;
    
public:
    /// 从文件解析编译命令
    bool parseFromFile(const std::string& filepath);
    
    /// 获取对应的bitcode文件列表
    std::vector<std::string> getBitcodeFiles() const;
    
    /// 获取所有编译命令
    const std::vector<CompileCommand>& getCommands() const { return commands; }
    
    /// 获取命令数量
    size_t getCommandCount() const { return commands.size(); }
    
    /// 清空命令列表
    void clear() { commands.clear(); }
    
    /// 获取指定索引的命令
    const CompileCommand* getCommand(size_t index) const {
        return (index < commands.size()) ? &commands[index] : nullptr;
    }
};

#endif // COMPILE_COMMANDS_PARSER_H
