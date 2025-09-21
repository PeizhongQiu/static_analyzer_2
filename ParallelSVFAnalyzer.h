//===- ParallelSVFAnalyzer.h - 保守的并行SVF分析器 -----------------------===//

#ifndef PARALLEL_SVF_ANALYZER_H
#define PARALLEL_SVF_ANALYZER_H

#include "SVFInterruptAnalyzer.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/IR/Instructions.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>

//===----------------------------------------------------------------------===//
// 保守的并行SVF分析器
// 
// 策略：
// 1. 文件I/O并行化 - 多线程加载bitcode文件
// 2. SVF分析完全串行化 - 避免所有SVF相关的并发问题
// 3. 结果合并并行化 - 安全地合并分析结果
//===----------------------------------------------------------------------===//

class ParallelSVFAnalyzer {
private:
    std::mutex results_mutex;
    std::vector<InterruptHandlerResult> all_results;
    std::atomic<int> completed_groups{0};
    std::atomic<int> total_groups{0};
    
public:
    /// 保守的并行分析（文件I/O并行，SVF串行）
    /// \param all_files 所有要分析的.bc文件
    /// \param handlers 中断处理函数名称列表
    /// \param num_threads 文件I/O线程数量
    /// \param files_per_group 每组文件数量
    /// \return 分析结果列表
    std::vector<InterruptHandlerResult> analyzeInParallel(
        const std::vector<std::string>& all_files,
        const std::vector<std::string>& handlers,
        size_t num_threads = 4,
        size_t files_per_group = 500);
    
private:
    /// 文件并行，SVF串行的分析策略
    void analyzeWithFileParallelOnly(
        const std::vector<std::vector<std::string>>& file_groups,
        const std::vector<std::string>& handlers,
        size_t num_threads);
    
    /// 并行加载单组文件
    void loadGroupFiles(
        const std::vector<std::string>& file_group,
        int group_id,
        std::vector<std::unique_ptr<Module>>& modules,
        std::unique_ptr<LLVMContext>& context);
    
    /// 串行分析单组（使用完整SVF）
    std::vector<InterruptHandlerResult> analyzeGroupSerially(
        const std::vector<std::unique_ptr<Module>>& modules,
        const std::vector<std::string>& handlers,
        int group_id);
    
    /// 将文件列表分组
    std::vector<std::vector<std::string>> groupFiles(
        const std::vector<std::string>& all_files,
        size_t files_per_group);
};

#endif // PARALLEL_SVF_ANALYZER_H
