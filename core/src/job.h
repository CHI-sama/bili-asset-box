// job.h —— 下载任务：线程池、事件队列、取消
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace bc {

using json = nlohmann::json;

class Job {
public:
    int32_t id = 0;
    json spec;
    std::string state = "pending";  // pending / running / done / failed / cancelled
    std::atomic<bool> cancel{false};

    //事件队列：worker 里 push，poll 里 drain
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<json> events;
    bool finished = false;

    //统计
    int total = 0;
    int done = 0;
    int skipped = 0;
    int failed = 0;
    int64_t bytes_done = 0;
    int64_t bytes_total = 0;

    std::thread worker;

    //析构时兜底：若线程还 joinable（比如调用方没走完 poll 就释放/
    //进程退出时静态对象析构），先请求取消再 join，否则 std::thread
    //析构会直接 std::terminate（表现为 "terminate called ..."）。
    ~Job();

    void push(json ev);
    std::vector<json> drain();
    void finish(const std::string& st);
};

class JobManager {
public:
    static JobManager& instance();

    int32_t create(const json& spec);
    std::shared_ptr<Job> get(int32_t id);
    bool start(int32_t id);
    bool cancel(int32_t id);
    bool free(int32_t id);

    //进程退出时收尾（join 所有线程）
    void shutdown();

private:
    JobManager() = default;
    std::mutex mutex_;
    std::map<int32_t, std::shared_ptr<Job>> jobs_;
    int32_t next_id_ = 1;
};

//任务执行体（在 worker 线程里跑）
void run_job(std::shared_ptr<Job> job);

}  // namespace bc
