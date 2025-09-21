//===- ParallelSVFAnalyzer.cpp - 并行SVF分析器实现 -----------------------===//

#include "ParallelSVFAnalyzer.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>
#include <algorithm>

using namespace llvm;

std::vector<InterruptHandlerResult> ParallelSVFAnalyzer::analyzeInParallel(
    const std::vector<std::string>& all_files,
    const std::vector<std::string>& handlers,
    size_t num_threads,
    size_t files_per_group) {
    
    all_results.clear();
    completed_groups = 0;
    
    outs() << "🚀 Starting parallel analysis...\n";
    outs() << "📊 Configuration:\n";
    outs() << "  Total files: " << all_files.size() << "\n";
    outs() << "  Files per group: " << files_per_group << "\n";
    outs() << "  Number of threads: " << num_threads << "\n";
    
    // 将文件分组
    auto file_groups = groupFiles(all_files, files_per_group);
    total_groups = file_groups.size();
    outs() << "  Total groups: " << total_groups << "\n\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 创建线程池
    std::vector<std::thread> threads;
    std::queue<int> group_queue;
    
    // 初始化组队列
    for (int i = 0; i < total_groups; ++i) {
        group_queue.push(i);
    }
    
    std::mutex queue_mutex;
    
    // 启动工作线程
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            while (true) {
                int group_id = -1;
                
                // 获取下一个组
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    if (group_queue.empty()) {
                        break;
                    }
                    group_id = group_queue.front();
                    group_queue.pop();
                }
                
                // 分析这个组
                analyzeGroup(file_groups[group_id], handlers, group_id);
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(end_time - start_time);
    
    outs() << "\n✅ Parallel analysis completed!\n";
    outs() << "⏱️  Total time: " << duration.count() << " minutes\n";
    outs() << "📊 Total results collected: " << all_results.size() << "\n";
    
    return all_results;
}

void ParallelSVFAnalyzer::analyzeGroup(
    const std::vector<std::string>& file_group,
    const std::vector<std::string>& handlers,
    int group_id) {
    
    auto group_start = std::chrono::high_resolution_clock::now();
    
    outs() << "🔄 Thread " << group_id << " starting with " << file_group.size() << " files\n";
    
    try {
        // 每个线程使用独立的LLVM上下文
        LLVMContext context;
        SVFInterruptAnalyzer analyzer(&context);
        
        // 加载这组文件
        if (!analyzer.loadBitcodeFiles(file_group)) {
            outs() << "❌ Thread " << group_id << " failed to load files\n";
            updateProgress(group_id);
            return;
        }
        
        // 初始化SVF（每个线程独立初始化）
        if (!analyzer.initializeSVF()) {
            outs() << "❌ Thread " << group_id << " failed to initialize SVF\n";
            updateProgress(group_id);
            return;
        }
        
        // 运行分析
        auto results = analyzer.analyzeInterruptHandlers(handlers);
        
        // 线程安全地合并结果
        mergeResults(results);
        
        auto group_end = std::chrono::high_resolution_clock::now();
        auto group_duration = std::chrono::duration_cast<std::chrono::minutes>(group_end - group_start);
        
        outs() << "✅ Thread " << group_id << " completed with " << results.size() 
               << " results in " << group_duration.count() << " minutes\n";
        
        updateProgress(group_id);
        
    } catch (const std::exception& e) {
        outs() << "❌ Thread " << group_id << " failed with exception: " << e.what() << "\n";
        updateProgress(group_id);
    }
}

void ParallelSVFAnalyzer::mergeResults(const std::vector<InterruptHandlerResult>& group_results) {
    std::lock_guard<std::mutex> lock(results_mutex);
    all_results.insert(all_results.end(), group_results.begin(), group_results.end());
}

std::vector<std::vector<std::string>> ParallelSVFAnalyzer::groupFiles(
    const std::vector<std::string>& all_files,
    size_t files_per_group) {
    
    std::vector<std::vector<std::string>> groups;
    
    for (size_t i = 0; i < all_files.size(); i += files_per_group) {
        std::vector<std::string> group;
        
        size_t end = std::min(i + files_per_group, all_files.size());
        for (size_t j = i; j < end; ++j) {
            group.push_back(all_files[j]);
        }
        
        groups.push_back(group);
    }
    
    return groups;
}

void ParallelSVFAnalyzer::updateProgress(int group_id) {
    int current = ++completed_groups;
    int total = total_groups;
    
    if (current % 1 == 0 || current == total) {  // 每完成一个组就更新
        outs() << "📊 Progress: " << current << "/" << total 
               << " (" << (current * 100 / total) << "%) groups completed\n";
    }
}
