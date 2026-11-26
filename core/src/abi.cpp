// abi.cpp —— C ABI 实现：导出函数、异常防火墙、内存所有权
//
//这里是唯一允许"跨语言"的边界：
//   * 所有 C++ 异常都在这里被捕获，翻译成错误码 + bc_last_error()；
//   * 所有返回给 Python 的字符串都由 malloc/new 分配，统一用 bc_free 释放；
//   * 不在 DLL 边界上暴露任何 STL 类型或 C++ 对象。
#include "../include/bili_core.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "api.h"
#include "job.h"
#include "login.h"
#include "ratelimit.h"
#include "util.h"
#include "win_util.h"

using nlohmann::json;

namespace {

constexpr const char* kVersion = "0.1.0";

std::mutex g_init_mutex;
bool g_initialized = false;

//每个线程各自的最后一次错误（Python 大多在同一个线程调用，够用）
thread_local std::string t_last_error = "{}";

//把 UTF-8 字符串塞进 char**（调用方用 bc_free 释放）
void set_out(char** out, const std::string& text) {
    if (!out) return;
    char* p = (char*)std::malloc(text.size() + 1);
    if (!p) {
        *out = nullptr;
        return;
    }
    std::memcpy(p, text.data(), text.size());
    p[text.size()] = '\0';
    *out = p;
}

//记录错误并返回错误 JSON
int fail(int code, const std::string& message, const std::string& op, char** out) {
    json e = {{"error", {{"code", code}, {"message", message}, {"op", op}}}};
    t_last_error = json{{"code", code}, {"message", message}, {"op", op}}.dump();
    set_out(out, e.dump());
    return code;
}

//统一的异常防火墙包装：fn 里可以随便抛
template <typename Fn>
int guard(const std::string& op, char** out, Fn&& fn) {
    try {
        json result = fn();
        set_out(out, result.dump());
        return 0;
    } catch (const bc::Error& e) {
        return fail(e.code, e.what(), op, out);
    } catch (const json::parse_error& e) {
        return fail(-3, std::string("JSON 解析失败: ") + e.what(), op, out);
    } catch (const std::exception& e) {
        return fail(-99, e.what(), op, out);
    } catch (...) {
        return fail(-99, "未知异常（已被内核拦截）", op, out);
    }
}

//进程退出 / DLL 卸载时确保工作线程已经 join，避免 Python 解释器退出时崩溃
struct AtExit {
    ~AtExit() {
        try {
            bc::JobManager::instance().shutdown();
        } catch (...) {
        }
    }
};
AtExit g_at_exit;

}  // namespace

//init / version
BC_API int BC_CALL bc_init(const char* config_json_utf8, char** out_json) {
    return guard("init", out_json, [&]() -> json {
        std::lock_guard<std::mutex> lk(g_init_mutex);
        json cfg = json::object();
        if (config_json_utf8 && *config_json_utf8) {
            cfg = json::parse(config_json_utf8);  //解析失败会被 guard 捕获为 -3
        }
        bc::ApiContext& c = bc::ctx();
        std::string data_dir = cfg.value("data_dir", "");
        if (data_dir.empty()) data_dir = ".";  //由调用方（GUI）保证传入绝对路径
        c.data_dir = data_dir;
        c.cache_dir = cfg.value("cache_dir", bc::path_join(data_dir, ".cache"));
        c.ca_bundle = cfg.value("ca_bundle", "");
        c.proxy = cfg.value("proxy", "");
        if (c.proxy.empty()) c.proxy = bc::read_system_proxy();

        bc::ensure_dir(c.data_dir);
        bc::ensure_dir(bc::path_join(data_dir, "logs"));
        bc::ensure_dir(c.cache_dir);

        std::string log_file = cfg.value("log_file", bc::path_join(data_dir, "logs/core.log"));
        bc::log_init(log_file);
        bc::log_msg("bc_init 完成，内核版本 " + std::string(kVersion));

        if (!cfg.value("user_agent", "").empty()) {
            c.http.set_user_agent(cfg.value("user_agent", ""));
        }
        //注意：ApiContext 里的 http 实例必须显式同步这些设置，
        //否则 API 请求会走"没有 CA / 没有代理"的默认值（TLS 会直接失败）
        c.http.set_ca_bundle(c.ca_bundle);
        c.http.set_proxy(c.proxy);

        //限速参数（保护 B 站服务器 + 降低账号被风控的概率）
        {
            json rate = cfg.value("rate", json::object());
            int api_ms = (int)rate.value("api_interval_ms", 500);
            int file_ms = (int)rate.value("file_interval_ms", 150);
            int kbps = (int)rate.value("per_file_kbps", 0);
            bc::RateLimiter::instance().configure(api_ms, file_ms, kbps);
        }

        //尝试恢复上次登录态（失败不影响启动）
        try {
            bc::load_session_from_disk();
        } catch (const std::exception& e) {
            bc::log_msg(std::string("恢复登录态失败: ") + e.what());
        }

        //匿名拿一次 nav（顺便缓存 WBI key），没网也不阻断启动
        try {
            bc::api_buvid(json::object());
            bc::api_nav(json::object());
        } catch (const std::exception& e) {
            bc::log_msg(std::string("启动时联网探测失败: ") + e.what());
        }

        g_initialized = true;
        json out = {{"version", kVersion},
                    {"data_dir", c.data_dir},
                    {"cache_dir", c.cache_dir},
                    {"logged_in", c.logged_in},
                    {"mid", c.mid},
                    {"uname", c.uname},
                    {"proxy", c.proxy}};
        return out;
    });
}

BC_API const char* BC_CALL bc_version(void) {
    return kVersion;
}

//同步调用
BC_API int BC_CALL bc_call(const char* op, const char* args_json_utf8, char** out_json) {
    std::string name = op ? op : "";
    return guard(name, out_json, [&]() -> json {
        if (name.empty()) throw bc::Error(-1, "op 不能为空");
        if (!g_initialized) throw bc::Error(-1, "内核未初始化，请先调用 bc_init");
        json args = json::object();
        if (args_json_utf8 && *args_json_utf8) args = json::parse(args_json_utf8);
        return bc::call_op(name, args);
    });
}

//任务
BC_API int BC_CALL bc_job_create(const char* spec_json_utf8, int32_t* out_job_id, char** out_json) {
    return guard("job_create", out_json, [&]() -> json {
        if (!g_initialized) throw bc::Error(-1, "内核未初始化");
        if (!out_job_id) throw bc::Error(-1, "out_job_id 不能为空");
        json spec = json::object();
        if (spec_json_utf8 && *spec_json_utf8) spec = json::parse(spec_json_utf8);
        std::string type = spec.value("type", "files");
        if (type != "files" && type != "video") throw bc::Error(-1, "不支持的任务类型: " + type);
        int32_t id = bc::JobManager::instance().create(spec);
        if (id <= 0) throw bc::Error(-99, "创建任务失败");
        *out_job_id = id;
        return json{{"job_id", id}, {"type", type}};
    });
}

BC_API int BC_CALL bc_job_start(int32_t job_id, char** out_json) {
    return guard("job_start", out_json, [&]() -> json {
        if (!bc::JobManager::instance().start(job_id)) throw bc::Error(-1, "任务不存在或不可启动");
        return json{{"job_id", job_id}, {"state", "running"}};
    });
}

BC_API int BC_CALL bc_job_cancel(int32_t job_id, char** out_json) {
    return guard("job_cancel", out_json, [&]() -> json {
        if (!bc::JobManager::instance().cancel(job_id)) throw bc::Error(-1, "任务不存在");
        return json{{"job_id", job_id}, {"cancelled", true}};
    });
}

BC_API int BC_CALL bc_job_poll(int32_t job_id, int32_t timeout_ms, char** out_json) {
    return guard("job_poll", out_json, [&]() -> json {
        auto job = bc::JobManager::instance().get(job_id);
        if (!job) throw bc::Error(-1, "任务不存在");
        {
            //最多等 timeout_ms，期间有事件或任务结束就醒
            std::unique_lock<std::mutex> lk(job->mutex);
            if (job->events.empty() && !job->finished && timeout_ms > 0) {
                job->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]() {
                    return !job->events.empty() || job->finished;
                });
            } else if (job->events.empty() && !job->finished) {
                job->cv.wait_for(lk, std::chrono::milliseconds(1));
            }
        }
        json events = json::array();
        for (auto& e : job->drain()) events.push_back(e);
        json out = {{"events", events},
                    {"finished", job->finished},
                    {"state", job->state},
                    {"total", job->total},
                    {"done", job->done},
                    {"skipped", job->skipped},
                    {"failed", job->failed},
                    {"bytes_done", job->bytes_done}};
        return out;
    });
}

BC_API int BC_CALL bc_job_free(int32_t job_id, char** out_json) {
    return guard("job_free", out_json, [&]() -> json {
        if (!bc::JobManager::instance().free(job_id)) throw bc::Error(-1, "任务不存在");
        return json{{"job_id", job_id}, {"freed", true}};
    });
}

//杂项
BC_API int BC_CALL bc_last_error(char** out_json) {
    if (!out_json) return -1;
    set_out(out_json, t_last_error);
    return 0;
}

BC_API void BC_CALL bc_free(char* p) {
    std::free(p);
}

BC_API int BC_CALL bc_sha256_file(const char* path_utf8, char** out_hex) {
    return guard("sha256_file", out_hex, [&]() -> json {
        if (!path_utf8 || !*path_utf8) throw bc::Error(-1, "path 不能为空");
        set_out(out_hex, bc::sha256_file_hex(path_utf8));
        return json{{"ok", true}};
    });
}

BC_API int BC_CALL bc_dpapi_protect(const char* data_utf8, char** out_b64) {
    return guard("dpapi_protect", out_b64, [&]() -> json {
        if (!data_utf8) throw bc::Error(-1, "data 不能为空");
        set_out(out_b64, bc::b64_encode(bc::dpapi_protect(data_utf8)));
        return json{{"ok", true}};
    });
}

BC_API int BC_CALL bc_dpapi_unprotect(const char* b64, char** out_utf8) {
    return guard("dpapi_unprotect", out_utf8, [&]() -> json {
        if (!b64) throw bc::Error(-1, "b64 不能为空");
        set_out(out_utf8, bc::dpapi_unprotect(bc::b64_decode(b64)));
        return json{{"ok", true}};
    });
}
