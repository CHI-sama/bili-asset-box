// ratelimit.cpp —— 全局请求限速器的实现
#include "ratelimit.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

#include "util.h"

namespace bc {

namespace {

// 0..max 的随机抖动，给固定节奏加点噪声，避免看起来像脚本
int jitter_ms(int max) {
    if (max <= 0) return 0;
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, max);
    return dist(rng);
}

}  // namespace

RateLimiter& RateLimiter::instance() {
    static RateLimiter limiter;
    return limiter;
}

void RateLimiter::configure(int api_interval_ms, int file_interval_ms, int per_file_kbps) {
    std::lock_guard<std::mutex> lk(mu_);
    //下限兜底：不允许把间隔配成 0（那等于没有限速）
    api_interval_ms_ = std::max(api_interval_ms, 200);
    file_interval_ms_ = std::max(file_interval_ms, 0);
    per_file_kbps_ = std::max(per_file_kbps, 0);
    log_msg("限速设置: api间隔=" + std::to_string(api_interval_ms_) + "ms 文件间隔=" +
            std::to_string(file_interval_ms_) + "ms 单文件=" +
            (per_file_kbps_ > 0 ? std::to_string(per_file_kbps_) + "KB/s" : "不限"));
}

int RateLimiter::api_interval_ms() const {
    std::lock_guard<std::mutex> lk(mu_);
    return api_interval_ms_;
}

int RateLimiter::file_interval_ms() const {
    std::lock_guard<std::mutex> lk(mu_);
    return file_interval_ms_;
}

int RateLimiter::per_file_kbps() const {
    std::lock_guard<std::mutex> lk(mu_);
    return per_file_kbps_;
}

void RateLimiter::acquire(RequestKind kind) {
    //循环直到真正拿到名额：期间可能碰到冷却，也可能被重新配置
    for (;;) {
        int64_t wait_ms = 0;
        {
            std::lock_guard<std::mutex> lk(mu_);
            int64_t now = now_ms();

            // 1) 全局冷却优先：所有线程一起歇着
            if (cooldown_until_ms_ > now) {
                wait_ms = cooldown_until_ms_ - now;
            } else {
                int64_t& slot = (kind == RequestKind::Api) ? next_api_ms_ : next_file_ms_;
                int interval = (kind == RequestKind::Api) ? api_interval_ms_ : file_interval_ms_;

                if (slot <= now) {
                    //占下这个时间片；额外加一点抖动
                    slot = now + interval + jitter_ms(kind == RequestKind::Api ? 120 : 80);
                    return;
                }
                wait_ms = slot - now;
            }
        }
        //分片 sleep，避免长时间不可中断
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<int64_t>(wait_ms, 500)));
    }
}

void RateLimiter::cool_down(int64_t ms, const std::string& reason) {
    if (ms <= 0) return;
    std::lock_guard<std::mutex> lk(mu_);
    int64_t until = now_ms() + ms;
    if (until > cooldown_until_ms_) cooldown_until_ms_ = until;
    log_msg("触发限速冷却 " + std::to_string(ms) + "ms（" + reason + "），期间暂停所有请求");
}

bool RateLimiter::in_cooldown() const {
    std::lock_guard<std::mutex> lk(mu_);
    return cooldown_until_ms_ > now_ms();
}

int64_t RateLimiter::cool_remaining_ms() const {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t left = cooldown_until_ms_ - now_ms();
    return left > 0 ? left : 0;
}

void RateLimiter::throttle(int64_t done_bytes, int64_t elapsed_ms) {
    int kbps = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        kbps = per_file_kbps_;
    }
    if (kbps <= 0 || done_bytes <= 0) return;

    //按目标速率算“现在最多应该下到多少字节”，超前就等一会
    const int64_t rate = (int64_t)kbps * 1024;  //字节/秒
    int64_t expected_ms = done_bytes * 1000 / rate;
    int64_t behind_ms = expected_ms - elapsed_ms;
    if (behind_ms > 40) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<int64_t>(behind_ms, 300)));
    }
}

}  // namespace bc
