// ratelimit.h —— 全局请求限速器
//
//目的有两个：
//   1) 对 B 站服务器友好，不做“零间隔连发”；
//   2) 降低账号被风控（412 / 429 / -352）的概率。
//
//设计要点：
//   - API 读接口和文件下载使用两套独立的最小间隔；
//   - 命中风控信号后进入全局冷却，所有线程一起等待；
//   - 单文件还可以按字节限速（默认关，设置里可开）。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace bc {

//请求类型：读接口比拉文件需要更保守的间隔
enum class RequestKind {
    Api,   //搜索、拉列表、取 playurl 这类读接口
    File,  //图片 / 视频分片下载
};

class RateLimiter {
public:
    static RateLimiter& instance();

    // api_interval_ms   两次 API 请求之间的最小间隔（默认 500ms）
    // file_interval_ms  两个文件下载“启动”之间的最小间隔（默认 150ms）
    // per_file_kbps     单个文件的速率上限（KB/s，0 表示不限，默认 0）
    void configure(int api_interval_ms, int file_interval_ms, int per_file_kbps);

    int api_interval_ms() const;
    int file_interval_ms() const;
    int per_file_kbps() const;

    //申请一次请求名额：必要时 sleep 到允许的时刻（会尊重全局冷却）
    void acquire(RequestKind kind);

    //命中风控：让所有线程一起停下来。reason 只用于日志。
    void cool_down(int64_t ms, const std::string& reason);
    bool in_cooldown() const;
    int64_t cool_remaining_ms() const;

    //按字节限速：已完成 done_bytes 却只花了 elapsed_ms，超前就原地等待
    void throttle(int64_t done_bytes, int64_t elapsed_ms);

private:
    RateLimiter() = default;
    RateLimiter(const RateLimiter&) = delete;
    RateLimiter& operator=(const RateLimiter&) = delete;

    mutable std::mutex mu_;
    int api_interval_ms_ = 500;
    int file_interval_ms_ = 150;
    int per_file_kbps_ = 0;

    //下一次允许发起请求的绝对时间（毫秒）
    int64_t next_api_ms_ = 0;
    int64_t next_file_ms_ = 0;
    //全局冷却截止时间
    int64_t cooldown_until_ms_ = 0;
};

}  // namespace bc
