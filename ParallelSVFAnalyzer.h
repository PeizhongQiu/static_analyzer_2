//===- ParallelSVFAnalyzer.h - 并行SVF分析器 -----------------------------===//

#ifndef PARALLEL_SVF_ANALYZER_H
#define PARALLEL_SVF_ANALYZER_H

#include "SVFInterruptAnalyzer.h"
#include <thread>
#include <future>
#include <mutex>
#include <atomic>

class ParallelSVFAnalyzer {
private:
    std::mutex results_mutex;
    std::vector<InterruptHandlerResult> all_results;
    std::atomic<int> completed_groups{0};
    std::atomic<int> total_groups{0};
    
public:
    /// 并行分析多组.bc文件
    std::vector<InterruptHandlerResult> analyzeInParallel(
        const std::vector<std::string>& all_files,
        const std::vector<std::string>& handlers,
        size_t num_threads = 4,
        size_t files_per_group = 500);
    
private:
    /// 单个线程的分析任务
    void analyzeGroup(
        const std::vector<std::string>& file_group,
        const std::vector<std::string>& handlers,
        int group_id);
    
    /// 合并结果
    void mergeResults(const std::vector<InterruptHandlerResult>& group_results);
    
    /// 将文件列表分组
    std::vector<std::vector<std::string>> groupFiles(
        const std::vector<std::string>& all_files,
        size_t files_per_group);
        
    /// 进度监控
    void updateProgress(int group_id);
};

#endif // PARALLEL_SVF_ANALYZER_H
