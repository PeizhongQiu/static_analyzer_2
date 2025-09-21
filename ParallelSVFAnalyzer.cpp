//===- ParallelSVFAnalyzer.cpp - 保守的并行SVF分析器 -----------------------===//

#include "ParallelSVFAnalyzer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include <chrono>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <sstream>

using namespace llvm;

std::vector<InterruptHandlerResult> ParallelSVFAnalyzer::analyzeInParallel(
    const std::vector<std::string>& all_files,
    const std::vector<std::string>& handlers,
    size_t num_threads,
    size_t files_per_group) {
    
    all_results.clear();
    completed_groups = 0;
    
    outs() << "🚀 Starting conservative parallel analysis...\n";
    outs() << "📊 Configuration:\n";
    outs() << "  Total files: " << all_files.size() << "\n";
    outs() << "  Files per group: " << files_per_group << "\n";
    outs() << "  Number of threads: " << num_threads << "\n";
    outs() << "  Mode: File I/O parallel, SVF completely serial\n";
    
    // 将文件分组
    auto file_groups = groupFiles(all_files, files_per_group);
    total_groups = file_groups.size();
    outs() << "  Total groups: " << total_groups << "\n\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 使用完全串行的SVF分析，只有文件I/O并行
    analyzeWithFileParallelOnly(file_groups, handlers, num_threads);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(end_time - start_time);
    
    outs() << "\n✅ Conservative parallel analysis completed!\n";
    outs() << "⏱️  Total time: " << duration.count() << " minutes\n";
    outs() << "📊 Total results collected: " << all_results.size() << "\n";
    
    return all_results;
}

void ParallelSVFAnalyzer::analyzeWithFileParallelOnly(
    const std::vector<std::vector<std::string>>& file_groups,
    const std::vector<std::string>& handlers,
    size_t num_threads) {
    
    outs() << "🔄 Starting file-parallel analysis with completely serial SVF...\n";
    
    // 第一步：并行预加载所有文件
    std::vector<std::vector<std::unique_ptr<Module>>> all_modules(file_groups.size());
    std::vector<std::unique_ptr<LLVMContext>> all_contexts(file_groups.size());
    
    outs() << "📦 Step 1: Parallel file loading...\n";
    
    // 创建线程池进行文件加载
    std::vector<std::thread> loading_threads;
    std::mutex loading_mutex;
    std::queue<int> loading_queue;
    
    // 初始化加载队列
    for (int i = 0; i < file_groups.size(); ++i) {
        loading_queue.push(i);
    }
    
    // 启动文件加载线程
    for (size_t t = 0; t < num_threads; ++t) {
        loading_threads.emplace_back([&]() {
            while (true) {
                int group_id = -1;
                
                // 获取下一个要加载的组
                {
                    std::lock_guard<std::mutex> lock(loading_mutex);
                    if (loading_queue.empty()) {
                        break;
                    }
                    group_id = loading_queue.front();
                    loading_queue.pop();
                }
                
                // 加载这组文件
                loadGroupFiles(file_groups[group_id], group_id, all_modules[group_id], all_contexts[group_id]);
            }
        });
    }
    
    // 等待所有文件加载完成
    for (auto& thread : loading_threads) {
        thread.join();
    }
    
    outs() << "✅ File loading completed\n";
    
    // 第二步：完全串行的SVF分析
    outs() << "🔧 Step 2: Serial SVF analysis...\n";
    
    for (size_t i = 0; i < file_groups.size(); ++i) {
        if (!all_modules[i].empty()) {
            outs() << "🔍 Analyzing group " << i << " with " << all_modules[i].size() << " modules\n";
            
            auto group_results = analyzeGroupSerially(all_modules[i], handlers, i);
            
            // 合并结果
            {
                std::lock_guard<std::mutex> lock(results_mutex);
                all_results.insert(all_results.end(), group_results.begin(), group_results.end());
            }
            
            completed_groups++;
            outs() << "📊 Progress: " << completed_groups << "/" << total_groups 
                   << " (" << (completed_groups * 100 / total_groups) << "%) groups completed\n";
        }
    }
}

void ParallelSVFAnalyzer::loadGroupFiles(
    const std::vector<std::string>& file_group,
    int group_id,
    std::vector<std::unique_ptr<Module>>& modules,
    std::unique_ptr<LLVMContext>& context) {
    
    // 获取线程ID字符串
    std::ostringstream thread_id_stream;
    thread_id_stream << std::this_thread::get_id();
    std::string thread_id_str = thread_id_stream.str();
    
    outs() << "📁 Thread " << thread_id_str << " loading group " << group_id 
           << " with " << file_group.size() << " files\n";
    
    try {
        // 创建独立的LLVM上下文
        context = std::make_unique<LLVMContext>();
        modules.clear();
        
        for (const auto& file : file_group) {
            try {
                auto BufferOrErr = MemoryBuffer::getFile(file);
                if (std::error_code EC = BufferOrErr.getError()) {
                    continue; // 跳过无法读取的文件
                }
                
                auto ModuleOrErr = parseBitcodeFile(BufferOrErr.get()->getMemBufferRef(), *context);
                if (!ModuleOrErr) {
                    continue; // 跳过无法解析的文件
                }
                
                auto M = std::move(ModuleOrErr.get());
                M->setModuleIdentifier(file);
                modules.push_back(std::move(M));
                
            } catch (...) {
                continue; // 跳过任何异常
            }
        }
        
        outs() << "✅ Thread " << thread_id_str << " loaded " << modules.size() 
               << " modules for group " << group_id << "\n";
        
    } catch (const std::exception& e) {
        outs() << "❌ Thread " << thread_id_str << " failed to load group " << group_id 
               << ": " << e.what() << "\n";
    }
}

std::vector<InterruptHandlerResult> ParallelSVFAnalyzer::analyzeGroupSerially(
    const std::vector<std::unique_ptr<Module>>& modules,
    const std::vector<std::string>& handlers,
    int group_id) {
    
    std::vector<InterruptHandlerResult> results;
    
    if (modules.empty()) {
        outs() << "⚠️  Group " << group_id << " has no modules to analyze\n";
        return results;
    }
    
    try {
        auto group_start = std::chrono::high_resolution_clock::now();
        
        // 创建SVF分析器（在主线程中，完全串行）
        LLVMContext temp_context;  // 创建临时上下文用于SVF
        SVFInterruptAnalyzer analyzer(&temp_context);
        
        // 将模块文件路径提供给分析器
        std::vector<std::string> module_files;
        for (const auto& M : modules) {
            module_files.push_back(M->getModuleIdentifier());
        }
        
        // 让SVF分析器重新加载文件（确保在正确的上下文中）
        if (!analyzer.loadBitcodeFiles(module_files)) {
            outs() << "⚠️  Group " << group_id << " failed to load files for SVF\n";
            return results;
        }
        
        // 初始化SVF（完全串行，无并发）
        if (!analyzer.initializeSVF()) {
            outs() << "⚠️  Group " << group_id << " failed to initialize SVF\n";
            return results;
        }
        
        // 运行SVF分析
        results = analyzer.analyzeInterruptHandlers(handlers);
        
        auto group_end = std::chrono::high_resolution_clock::now();
        auto group_duration = std::chrono::duration_cast<std::chrono::seconds>(group_end - group_start);
        
        outs() << "✅ Group " << group_id << " completed SVF analysis with " << results.size() 
               << " results in " << group_duration.count() << " seconds\n";
        
    } catch (const std::exception& e) {
        outs() << "❌ Group " << group_id << " SVF analysis failed: " << e.what() << "\n";
    } catch (...) {
        outs() << "❌ Group " << group_id << " SVF analysis failed with unknown exception\n";
    }
    
    return results;
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
