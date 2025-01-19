#include <iostream>
#include <string>
#include <cstdlib>
#include <cxxopts.hpp>
#include <atomic>
#include <mutex>
#include <chrono>
#include <vector>
#include "httplib.h"
#include "threadpool.hpp"

std::atomic<int> successCount;
std::atomic<int> failCount;
std::mutex Mutex;
std::vector<double> responseTimes;

struct Config
{
    std::string url_;   // 目标url
    int clients_ = 1;   // 并发客户端数量
    int duration_ = 30; // 运行时间
};
/**/
Config parseArguments(int argc, char *argv[])
{
    // 1. 定义一个cxxopts对象
    cxxopts::Options options("HttpBench", "A modern Http benchmarking tool!");

    // 2. 定义命令行选项
    options.add_options()("u,url", "Target URL", cxxopts::value<std::string>())                 // 目标URL
        ("c,clients", "Number of clients", cxxopts::value<int>()->default_value("1"))           // 并发客户端数量
        ("t,time", "Benchmark duration in seconds", cxxopts::value<int>()->default_value("30")) // 运行时间
        ("h,help", "Print help");                                                               // 帮助信息

    // 3.解析命令行参数
    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        exit(0);
    }

    Config config;
    if (result.count("url"))
    {
        config.url_ = result["url"].as<std::string>();
    }
    else
    {
        throw std::invalid_argument("Target URL is required !");
    }

    config.clients_ = result["clients"].as<int>();
    config.duration_ = result["time"].as<int>();

    return config;
}
void sendRequest(const std::string url)
{
    try
    {
        auto start = std::chrono::high_resolution_clock::now();

        // 1. 解析URL
        httplib::Result res;
        {
            std::lock_guard<std::mutex> guard(Mutex);
            httplib::Client cli(url.c_str());
            cli.set_read_timeout(5, 0); // 设置超时为 5 秒
            cli.set_write_timeout(5, 0);

            // 2. 发送Get请求
            res = cli.Get("/");
        }
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
        if (res)
        {
            ++successCount;
            {
                std::lock_guard<std::mutex> guard(Mutex);
                responseTimes.push_back(elapsed);
            }
            std::cout << "Response Code:" << res->status << std::endl;
        }
        else
        {
            ++failCount;
            std::cerr << "Request failed: " << httplib::to_string(res.error()) << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }
}
int main(int argc, char *argv[])
{
    try
    {
        // 解析命令行参数
        Config config = parseArguments(argc, argv);
        // 启动线程池
        auto pool = ThreadPool::getInstance();
        pool->start(config.clients_, PoolMode::MODE_FIXED);
        std::vector<std::future<void>> futures;
        futures.resize(1000);
        for (int i = 0; i < 1000; i++)
        {
            futures[i] = pool->submitTask(sendRequest, config.url_);
        }

        for (int i = 0; i < 1000; i++)
        {
            futures[i].get();
        }

        std::cout << "Benchmarking finished." << std::endl;

        // 打印统计结果
        std::cout << "Requests completed: " << successCount << std::endl;
        std::cout << "Requests failed: " << failCount << std::endl;

        if (!responseTimes.empty())
        {
            auto minTime = *std::min_element(responseTimes.begin(), responseTimes.end());
            auto maxTime = *std::max_element(responseTimes.begin(), responseTimes.end());
            auto avgTime = std::accumulate(responseTimes.begin(), responseTimes.end(), 0.0) / responseTimes.size();

            std::cout << "Min response time: " << minTime << " ms" << std::endl;
            std::cout << "Max response time: " << maxTime << " ms" << std::endl;
            std::cout << "Avg response time: " << avgTime << " ms" << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }

    return 0;
}