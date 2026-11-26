// http.h —— 基于 libcurl 的 HTTP 会话（Cookie 自动保存、重试、WBI 签名）
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bc {

struct HttpResponse {
    long status = 0;
    std::string body;
    std::map<std::string, std::string> headers;  //全部小写 key
};

//下载进度回调：返回 false 表示取消
using ProgressFn = std::function<bool(int64_t done, int64_t total)>;

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    //全局初始化/清理（进程内只做一次）
    static void global_init();
    static void global_cleanup();

    void set_proxy(const std::string& proxy) { proxy_ = proxy; }
    void set_ca_bundle(const std::string& path) { ca_bundle_ = path; }
    void set_user_agent(const std::string& ua) { ua_ = ua; }
    void set_referer(const std::string& referer) { referer_ = referer; }
    void set_timeout(int connect_ms, int total_sec) {
        connect_timeout_ms_ = connect_ms;
        total_timeout_sec_ = total_sec;
    }

    // Cookie：整串读取/写入（"k=v; k2=v2"），也可以单独取值
    std::string cookie_string() const;
    void set_cookie_string(const std::string& cookies);
    std::string cookie_value(const std::string& name) const;
    void clear_cookies();

    //同步 GET；内部按情况重试（5xx / 429 / 连接错误）
    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& headers = {},
                     int retries = 2);

    //带签名的 GET：先 refresh 过 wbi key 才能调用（见 wbi.h）
    HttpResponse get_api(const std::string& api, std::map<std::string, std::string> params);

    //文件下载：支持续传（.part）、进度回调。返回最终 HTTP 状态码。
    //若 status==200/206 且 done，则文件已就位。
    long download_file(const std::string& url, const std::string& dest_path,
                       ProgressFn progress, int retries = 3);

    //统计（调试用）
    int64_t request_count() const { return request_count_; }

private:
    HttpResponse perform(const std::string& url, const std::string& method,
                         const std::string& post_body,
                         const std::map<std::string, std::string>& headers,
                         ProgressFn progress, bool is_file, const std::string& dest,
                         int retries);
    void absorb_set_cookie(const std::string& header_line);

    std::map<std::string, std::string> cookies_;
    std::string proxy_;
    std::string ca_bundle_;
    std::string ua_;
    std::string referer_ = "https://www.bilibili.com/";
    int connect_timeout_ms_ = 10000;
    int total_timeout_sec_ = 30;
    int64_t request_count_ = 0;
};

//从 URL 里取出 query 参数（用于调试/日志）
std::string url_query_value(const std::string& url, const std::string& key);

//把参数 map 拼成 query string（不做排序，签名逻辑在 wbi.cpp 里单独处理）
std::string build_query(const std::map<std::string, std::string>& params);

}  // namespace bc
