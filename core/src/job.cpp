// job.cpp —— 下载任务实现
#include "job.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "api.h"
#include "http.h"
#include "ratelimit.h"
#include "util.h"

#ifdef _WIN32
#  include <process.h>
#  include <windows.h>
#endif

namespace bc {

namespace {

//单个文件的下载结果
struct FileResult {
    bool ok = false;
    bool skipped = false;
    bool cancelled = false;
    std::string message;
    int code = 0;
    int64_t bytes = 0;
};

//跑一个外部命令并等它结束，返回进程退出码（失败 -1）。
//
//为什么不用 std::system：Windows 上 std::system 会拼成 `cmd /c "..."`，当命令
//以带空格的引号路径开头时，cmd 会吃掉最外层引号，导致 "文件名语法不正确"。
//直接 CreateProcessW 没有这个问题，也能正确处理中文路径。
int run_process(const std::string& cmdline) {
#ifdef _WIN32
    std::wstring w = utf8_to_wide(cmdline);
    std::vector<wchar_t> buf(w.begin(), w.end());
    buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;  //不弹黑框
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#else
    return std::system(cmdline.c_str());
#endif
}

std::string resolve_ffmpeg(const std::string& given) {
    if (!given.empty() && file_exists(given)) return given;
    //常见位置：PATH 里 / 用户目录下的便携版
    const char* candidates[] = {
#ifdef _WIN32
        "ffmpeg.exe",
#endif
        "ffmpeg",
    };
    for (const char* c : candidates) {
        std::string cmd = std::string(c) + " -hide_banner -version";
        if (run_process(cmd) == 0) return c;
    }
    //默认安装位置（只读探测，不修改）
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH) > 0) {
        std::string guess = std::string(buf) + "\\ffmpeg\\bin\\ffmpeg.exe";
        if (file_exists(guess)) return guess;
    }
#endif
    return {};
}

//用 ffmpeg 合并音视频
bool merge_av(const std::string& ffmpeg, const std::string& video, const std::string& audio,
              const std::string& out) {
    std::string cmd = "\"" + ffmpeg + "\" -hide_banner -loglevel error -y -i \"" + video +
                      "\" -i \"" + audio + "\" -c copy -movflags +faststart \"" + out + "\"";
    log_msg("ffmpeg: " + cmd);
    return run_process(cmd) == 0;
}

//在 worker 线程里下载一个文件，负责发事件
void download_one(const std::shared_ptr<Job>& job, int index, const json& item,
                  const std::string& fallback_dir) {
    std::string url = item.value("url", "");
    std::string path = item.value("path", "");
    std::string name = item.value("name", "");
    std::string wanted_sha = item.value("sha256", "");
    std::string referer = item.value("referer", "");
    if (path.empty()) {
        if (name.empty()) name = sanitize_filename(filename_from_url(url));
        path = path_join(fallback_dir, name);
    }
    if (url.empty()) {
        job->push({{"type", "file_error"}, {"index", index}, {"message", "缺少 url"}});
        std::lock_guard<std::mutex> lk(job->mutex);
        ++job->failed;
        job->cv.notify_all();
        return;
    }
    if (job->cancel) {
        job->push({{"type", "file_error"}, {"index", index}, {"message", "已取消"}, {"cancelled", true}});
        std::lock_guard<std::mutex> lk(job->mutex);
        ++job->failed;
        return;
    }

    //已存在且校验通过 => 跳过
    if (file_exists(path)) {
        bool skip = false;
        if (!wanted_sha.empty()) {
            try {
                skip = sha256_file_hex(path) == wanted_sha;
            } catch (...) {
                skip = false;
            }
        } else if (file_size(path) > 0 && !item.contains("size")) {
            skip = true;  //没有校验信息时按"文件名存在"跳过
        }
        if (skip) {
            job->push({{"type", "file_skip"}, {"index", index}, {"path", path},
                       {"bytes", (int64_t)file_size(path)}});
            std::lock_guard<std::mutex> lk(job->mutex);
            ++job->skipped;
            job->bytes_done += (int64_t)file_size(path);
            job->cv.notify_all();
            return;
        }
    }

    job->push({{"type", "file_start"}, {"index", index}, {"path", path}, {"url", url}});

    std::string part = path + ".part";
    //有校验信息时不要续传旧 .part（可能已经损坏）
    if (!wanted_sha.empty() && file_exists(part)) remove_file_quiet(part);

    HttpClient http;
    http.set_proxy(ctx().proxy);
    http.set_ca_bundle(ctx().ca_bundle);
    std::string cookies = ctx().http.cookie_string();
    if (!cookies.empty()) http.set_cookie_string(cookies);
    if (!referer.empty()) http.set_referer(referer);

    int64_t last_emit = 0;
    int64_t last_bytes = 0;
    auto progress = [&](int64_t done_bytes, int64_t total_bytes) -> bool {
        int64_t now = now_ms();
        if (now - last_emit >= 200) {
            last_emit = now;
            json ev = {{"type", "progress"}, {"index", index}, {"done", done_bytes},
                       {"total", total_bytes}, {"path", path},
                       {"bytes_delta", done_bytes - last_bytes}};
            last_bytes = done_bytes;
            job->push(ev);
        }
        return !job->cancel.load();
    };

    FileResult res;
    try {
        //先下到 .part，成功后再原子改名，避免 GUI 看到半截文件
        long status = http.download_file(url, part, progress);
        if (status < 200 || status >= 300) {
            res.message = "HTTP " + std::to_string(status);
            res.code = -2;
        } else {
            ensure_dir(path.substr(0, path.find_last_of("/\\")));
            if (file_exists(path)) remove_file_quiet(path);
            std::error_code ec;
            std::filesystem::rename(std::filesystem::u8path(part), std::filesystem::u8path(path), ec);
            if (ec) {
                res.message = "重命名失败: " + ec.message();
                res.code = -5;
            } else {
                res.ok = true;
                res.bytes = (int64_t)file_size(path);
                //校验
                if (!wanted_sha.empty()) {
                    std::string actual = sha256_file_hex(path);
                    if (actual != wanted_sha) {
                        res.ok = false;
                        res.code = -6;
                        res.message = "SHA-256 校验失败";
                        remove_file_quiet(path);
                    }
                }
                //文件头嗅探：图片/视频被风控替换成 HTML 时能及时发现
                if (res.ok) {
                    std::string head = read_text_file_head(path, 16);
                    if (head.rfind("<!DOCTYPE", 0) == 0 || head.rfind("<html", 0) == 0) {
                        res.ok = false;
                        res.code = -6;
                        res.message = "下载到的是网页而不是素材（可能被风控）";
                        remove_file_quiet(path);
                    }
                }
            }
        }
    } catch (const Error& e) {
        res.message = e.what();
        res.code = e.code;
        if (e.code == -7) res.cancelled = true;
    } catch (const std::exception& e) {
        res.message = e.what();
        res.code = -99;
    }

    //先更新计数（加锁），再 push 事件。
    //注意：不能"持锁时再调 push"，push 自己也要拿同一把锁，会自我死锁。
    if (res.ok) {
        {
            std::lock_guard<std::mutex> lk(job->mutex);
            ++job->done;
            job->bytes_done += res.bytes;
        }
        job->push({{"type", "file_done"}, {"index", index}, {"path", path},
                   {"bytes", res.bytes}, {"name", filename_from_url(path)}});
    } else {
        {
            std::lock_guard<std::mutex> lk(job->mutex);
            ++job->failed;
        }
        job->push({{"type", "file_error"}, {"index", index}, {"path", path},
                   {"message", res.message}, {"code", res.code},
                   {"cancelled", res.cancelled}});
    }
}

}  // namespace

//析构兜底：如果线程还 joinable，先请求取消再 join。
//没有这段代码时，进程退出/任务对象被提前销毁会出现
// "terminate called without an active exception"（std::thread 析构期 terminate）。
Job::~Job() {
    cancel = true;
    if (worker.joinable()) {
        //理论上不会在自己线程里析构，但防御一下，避免自我 join 死锁
        if (worker.get_id() == std::this_thread::get_id()) {
            worker.detach();
        } else {
            worker.join();
        }
    }
}

void Job::push(json ev) {
    std::lock_guard<std::mutex> lk(mutex);
    events.push_back(std::move(ev));
    cv.notify_all();
}

std::vector<json> Job::drain() {
    std::lock_guard<std::mutex> lk(mutex);
    std::vector<json> out;
    out.swap(events);
    return out;
}

void Job::finish(const std::string& st) {
    std::lock_guard<std::mutex> lk(mutex);
    state = st;
    finished = true;
    cv.notify_all();
}

JobManager& JobManager::instance() {
    static JobManager mgr;
    return mgr;
}

int32_t JobManager::create(const json& spec) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto job = std::make_shared<Job>();
    job->id = next_id_++;
    job->spec = spec;
    jobs_[job->id] = job;
    return job->id;
}

std::shared_ptr<Job> JobManager::get(int32_t id) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = jobs_.find(id);
    return it == jobs_.end() ? nullptr : it->second;
}

bool JobManager::start(int32_t id) {
    auto job = get(id);
    if (!job) return false;
    if (job->state == "running") return true;
    if (job->finished) return false;
    job->state = "running";
    job->worker = std::thread([job]() { run_job(job); });
    return true;
}

bool JobManager::cancel(int32_t id) {
    auto job = get(id);
    if (!job) return false;
    job->cancel = true;
    return true;
}

bool JobManager::free(int32_t id) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) return false;
        job = it->second;
        jobs_.erase(it);
    }
    job->cancel = true;
    if (job->worker.joinable()) job->worker.join();
    return true;
}

void JobManager::shutdown() {
    std::map<int32_t, std::shared_ptr<Job>> snapshot;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        snapshot = jobs_;
    }
    for (auto& kv : snapshot) {
        kv.second->cancel = true;
    }
    for (auto& kv : snapshot) {
        if (kv.second->worker.joinable()) kv.second->worker.join();
    }
}

//执行体
void run_job(std::shared_ptr<Job> job) {
    try {
        std::string type = job->spec.value("type", "files");
        std::string dir = job->spec.value("out_dir", "");
        if (!dir.empty()) ensure_dir(dir);

        if (type == "video") {
            //视频下载：playurl -> 视频流 + 音频流 -> ffmpeg 合并
            std::string bvid = job->spec.value("bvid", "");
            int64_t cid = job->spec.value("cid", (int64_t)0);
            int64_t qn = job->spec.value("qn", (int64_t)80);
            std::string name = job->spec.value("name", bvid);
            std::string ffmpeg = resolve_ffmpeg(job->spec.value("ffmpeg", ""));
            if (ffmpeg.empty()) throw Error(-8, "未找到 ffmpeg，请在设置里指定路径");

            job->push({{"type", "log"}, {"message", "获取视频流信息…"}});
            json play = api_video_playurl({{"bvid", bvid}, {"cid", cid}, {"qn", qn}});
            json videos = play.value("videos", json::array());
            json audios = play.value("audios", json::array());
            if (videos.empty()) throw Error(-3, "没有拿到可用的视频流（可能需要大会员或登录）");

            json best = videos[0];
            std::string vurl = best.value("url", "");
            if (vurl.empty()) throw Error(-3, "视频流地址为空");
            std::string aurl = audios.empty() ? "" : audios[0].value("url", "");

            ensure_dir(dir);
            std::string safe = sanitize_filename(name, 80);
            std::string vtmp = path_join(dir, safe + ".video.m4s");
            std::string atmp = path_join(dir, safe + ".audio.m4s");
            std::string out = path_join(dir, safe + ".mp4");

            job->total = aurl.empty() ? 1 : 2;
            job->bytes_total = 0;

            auto progress = [&](int64_t done_bytes, int64_t total_bytes) -> bool {
                static int64_t last = 0;
                int64_t now = now_ms();
                if (now - last >= 200) {
                    last = now;
                    job->push({{"type", "progress"}, {"index", 0}, {"done", done_bytes},
                               {"total", total_bytes}});
                }
                return !job->cancel.load();
            };

            HttpClient http;
            http.set_proxy(ctx().proxy);
            http.set_ca_bundle(ctx().ca_bundle);
            http.set_cookie_string(ctx().http.cookie_string());
            http.set_referer("https://www.bilibili.com/");

            job->push({{"type", "file_start"}, {"index", 0}, {"path", vtmp},
                       {"url", vurl}, {"name", safe + " 视频流"}});
            long st = http.download_file(vurl, vtmp, progress);
            if (st < 200 || st >= 300) throw Error(-2, "视频流下载失败 HTTP " + std::to_string(st));
            job->done = 1;

            if (!aurl.empty()) {
                job->push({{"type", "file_start"}, {"index", 1}, {"path", atmp},
                           {"url", aurl}, {"name", safe + " 音频流"}});
                st = http.download_file(aurl, atmp, progress);
                if (st < 200 || st >= 300) throw Error(-2, "音频流下载失败 HTTP " + std::to_string(st));
                job->done = 2;
            }

            job->push({{"type", "log"}, {"message", "正在用 ffmpeg 合并…"}});
            bool merged = false;
            if (aurl.empty()) {
                std::error_code ec;
                std::filesystem::rename(std::filesystem::u8path(vtmp), std::filesystem::u8path(out), ec);
                merged = !ec;
            } else {
                merged = merge_av(ffmpeg, vtmp, atmp, out);
            }
            if (!merged) throw Error(-99, "ffmpeg 合并失败（音频/视频流可能不完整）");
            remove_file_quiet(vtmp);
            remove_file_quiet(atmp);
            job->push({{"type", "file_done"}, {"index", 2}, {"path", out},
                       {"bytes", (int64_t)file_size(out)}, {"name", safe + ".mp4"}});
            job->push({{"type", "done"}, {"ok", 1}, {"failed", 0},
                       {"bytes", (int64_t)file_size(out)}, {"output", out}});
            job->finish("done");
            return;
        }

        //通用文件列表
        json items = job->spec.value("items", json::array());
        //并发默认 6（社区验证过 8 也算温和，上限就卡在 8，绝不会"无限开线程"）
        int concurrency = std::max(1, std::min(8, (int)job->spec.value("concurrency", 6)));
        //允许单个任务覆盖全局限速（GUI 里"本次任务更快/更慢"用）
        if (job->spec.contains("api_interval_ms") || job->spec.contains("file_interval_ms") ||
            job->spec.contains("per_file_kbps")) {
            RateLimiter::instance().configure(
                (int)job->spec.value("api_interval_ms", RateLimiter::instance().api_interval_ms()),
                (int)job->spec.value("file_interval_ms", RateLimiter::instance().file_interval_ms()),
                (int)job->spec.value("per_file_kbps", RateLimiter::instance().per_file_kbps()));
        }
        job->total = (int)items.size();
        job->bytes_total = 0;
        if (items.empty()) {
            job->push({{"type", "done"}, {"ok", 0}, {"failed", 0}, {"bytes", 0}});
            job->finish("done");
            return;
        }

        std::atomic<size_t> next_index{0};
        std::vector<std::thread> pool;
        int workers = std::min<int>(concurrency, (int)items.size());
        for (int w = 0; w < workers; ++w) {
            pool.emplace_back([job, &items, &next_index, dir]() {
                while (!job->cancel) {
                    size_t i = next_index.fetch_add(1);
                    if (i >= items.size()) break;
                    download_one(job, (int)i, items[i], dir);
                }
            });
        }
        for (auto& t : pool) t.join();

        std::string final_state = job->cancel ? "cancelled" : "done";
        job->push({{"type", "done"},
                   {"ok", job->done},
                   {"skipped", job->skipped},
                   {"failed", job->failed},
                   {"bytes", job->bytes_done},
                   {"cancelled", job->cancel.load()}});
        job->finish(final_state);
    } catch (const Error& e) {
        job->push({{"type", "fatal"}, {"message", e.what()}, {"code", e.code}});
        job->finish(e.code == -7 ? "cancelled" : "failed");
    } catch (const std::exception& e) {
        job->push({{"type", "fatal"}, {"message", e.what()}, {"code", -99}});
        job->finish("failed");
    }
}

}  // namespace bc
