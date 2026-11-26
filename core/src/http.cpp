// http.cpp —— libcurl 封装
#include "http.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#include <curl/curl.h>

#include "util.h"
#include "ratelimit.h"
#include "wbi.h"

namespace bc {

namespace {

std::once_flag g_curl_once;

size_t write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t write_to_file(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* f = static_cast<FILE*>(userdata);
    return fwrite(ptr, size, nmemb, f);
}

size_t header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    size_t len = size * nitems;
    std::string line(buffer, len);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(colon + 1));
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        //同名头（如多个 Set-Cookie）用 "\n" 连接，避免互相覆盖
        auto it = headers->find(key);
        if (it != headers->end()) {
            it->second += "\n" + val;
        } else {
            (*headers)[key] = val;
        }
    }
    return len;
}

struct ProgressCtx {
    ProgressFn fn;
    bool aborted = false;
    int64_t start_ms = 0;              //本次传输开始时间（用于字节限速）
    int64_t last_throttle_bytes = 0;   //上次做过限速判断的字节数
};

int xferinfo_cb(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<ProgressCtx*>(p);
    if (ctx->fn) {
        if (!ctx->fn((int64_t)dlnow, (int64_t)dltotal)) {
            ctx->aborted = true;
            return 1;  //非 0 表示中止
        }
    }
    //单文件限速：每 64KB 才判断一次，避免每次回调都 sleep 造成抖动
    if (dlnow - ctx->last_throttle_bytes >= 64 * 1024) {
        ctx->last_throttle_bytes = (int64_t)dlnow;
        RateLimiter::instance().throttle((int64_t)dlnow, now_ms() - ctx->start_ms);
    }
    return 0;
}

const char* kDefaultUA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

}  // namespace

void HttpClient::global_init() {
    std::call_once(g_curl_once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

void HttpClient::global_cleanup() {
    //进程退出前由 ABI 层调用；重复调用无副作用
    curl_global_cleanup();
}

HttpClient::HttpClient() { ua_ = kDefaultUA; }
HttpClient::~HttpClient() = default;

std::string HttpClient::cookie_string() const {
    std::string s;
    for (const auto& kv : cookies_) {
        if (!s.empty()) s += "; ";
        s += kv.first + "=" + kv.second;
    }
    return s;
}

void HttpClient::set_cookie_string(const std::string& cookies) {
    cookies_.clear();
    size_t pos = 0;
    while (pos < cookies.size()) {
        size_t semi = cookies.find(';', pos);
        std::string part = cookies.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        part = trim(part);
        auto eq = part.find('=');
        if (eq != std::string::npos) {
            std::string k = trim(part.substr(0, eq));
            std::string v = trim(part.substr(eq + 1));
            if (!k.empty()) cookies_[k] = v;
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
}

std::string HttpClient::cookie_value(const std::string& name) const {
    auto it = cookies_.find(name);
    return it == cookies_.end() ? std::string() : it->second;
}

void HttpClient::clear_cookies() { cookies_.clear(); }

void HttpClient::absorb_set_cookie(const std::string& header_value) {
    //一个 header 里可能有多条（用 \n 分隔）
    size_t pos = 0;
    while (pos < header_value.size()) {
        size_t nl = header_value.find('\n', pos);
        std::string line = header_value.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        auto semi = line.find(';');
        std::string pair = (semi == std::string::npos) ? line : line.substr(0, semi);
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string k = trim(pair.substr(0, eq));
            std::string v = trim(pair.substr(eq + 1));
            if (!k.empty() && !v.empty() && v != "deleted") cookies_[k] = v;
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
}

HttpResponse HttpClient::perform(const std::string& url, const std::string& method,
                                 const std::string& post_body,
                                 const std::map<std::string, std::string>& headers,
                                 ProgressFn progress, bool is_file, const std::string& dest,
                                 int retries) {
    HttpResponse resp;
    int attempt = 0;
    int64_t backoff_ms = 500;
    while (true) {
        ++attempt;
        resp = HttpResponse{};
        CURL* h = curl_easy_init();
        if (!h) throw Error(-99, "curl_easy_init 失败");

        struct curl_slist* hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, "Accept: */*");
        hdrs = curl_slist_append(hdrs, "Accept-Language: zh-CN,zh;q=0.9");
        std::string ua = "User-Agent: " + ua_;
        hdrs = curl_slist_append(hdrs, ua.c_str());
        if (!referer_.empty()) {
            std::string ref = "Referer: " + referer_;
            hdrs = curl_slist_append(hdrs, ref.c_str());
        }
        std::string cookie = cookie_string();
        if (!cookie.empty()) {
            std::string c = "Cookie: " + cookie;
            hdrs = curl_slist_append(hdrs, c.c_str());
        }
        for (const auto& kv : headers) {
            std::string line = kv.first + ": " + kv.second;
            hdrs = curl_slist_append(hdrs, line.c_str());
        }

        curl_easy_setopt(h, CURLOPT_URL, url.c_str());
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, (long)connect_timeout_ms_);
        curl_easy_setopt(h, CURLOPT_TIMEOUT, (long)(is_file ? 3600 : total_timeout_sec_));
        curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
        //我们的 libcurl 没编 zlib/brotli，不能请求压缩内容，
        //否则响应头会带 Content-Encoding: gzip 而 curl 无法解码
        curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "identity");
        // TLS：显式指定 CA bundle，避免 MinGW 下找不到系统证书
        if (!ca_bundle_.empty()) {
            curl_easy_setopt(h, CURLOPT_CAINFO, ca_bundle_.c_str());
        }
        if (!proxy_.empty()) {
            curl_easy_setopt(h, CURLOPT_PROXY, proxy_.c_str());
        }
        curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, header_cb);
        curl_easy_setopt(h, CURLOPT_HEADERDATA, &resp.headers);

        if (method == "POST") {
            curl_easy_setopt(h, CURLOPT_POST, 1L);
            curl_easy_setopt(h, CURLOPT_POSTFIELDS, post_body.c_str());
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)post_body.size());
        }

        ProgressCtx pctx;
        pctx.fn = progress;
        pctx.start_ms = now_ms();
        if (progress) {
            curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
            curl_easy_setopt(h, CURLOPT_XFERINFODATA, &pctx);
            curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
        }

        FILE* fp = nullptr;
        if (is_file) {
            //关键：MinGW 的 fopen 用 ANSI 代码页，中文/emoji 路径会打不开，
            //必须转成宽字符走 _wfopen
#ifdef _WIN32
            fp = _wfopen(utf8_to_wide(dest).c_str(), L"ab");
#else
            fp = fopen(dest.c_str(), "ab");
#endif
            if (!fp) {
                curl_slist_free_all(hdrs);
                curl_easy_cleanup(h);
                throw Error(-5, "无法打开下载文件: " + dest);
            }
            uint64_t existing = file_size(dest);
            curl_easy_setopt(h, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)existing);
            curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_to_file);
            curl_easy_setopt(h, CURLOPT_WRITEDATA, fp);
        } else {
            curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_to_string);
            curl_easy_setopt(h, CURLOPT_WRITEDATA, &resp.body);
        }

        //限速排队：读接口和文件下载各有一套间隔，必要时会在这里等一下
        RateLimiter::instance().acquire(is_file ? RequestKind::File : RequestKind::Api);

        ++request_count_;
        CURLcode rc = curl_easy_perform(h);

        if (is_file && fp) fclose(fp);

        long status = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
        resp.status = status;

        //吸收 Set-Cookie
        auto sc = resp.headers.find("set-cookie");
        if (sc != resp.headers.end()) absorb_set_cookie(sc->second);

        char* err = nullptr;
        curl_easy_getinfo(h, CURLINFO_EFFECTIVE_URL, &err);

        curl_slist_free_all(hdrs);
        curl_easy_cleanup(h);

        if (pctx.aborted) {
            throw Error(-7, "下载已取消");
        }

        // 412/429 是 B 站明确的“你太快了”信号：别硬刚，让全局停下来缓一缓
        if (status == 412 || status == 429) {
            int64_t cool = std::min<int64_t>(60000, 15000 * attempt);
            RateLimiter::instance().cool_down(
                cool, status == 412 ? "HTTP 412 风控" : "HTTP 429 请求过多");
        }

        //文件下载遇到错误页时，把已写入的内容丢掉，避免续传把 HTML 拼进素材
        if (is_file && (status == 412 || status == 429 || status >= 500)) {
            remove_file_quiet(dest);
        }

        bool retryable_http = (status == 412 || status == 429 || status >= 500);
        bool retryable_net = (rc != CURLE_OK);
        bool retryable = retryable_http || (retryable_net && rc != CURLE_ABORTED_BY_CALLBACK);

        if (!retryable || attempt > retries) {
            if (rc != CURLE_OK) {
                std::string msg = curl_easy_strerror(rc);
                if (is_file) {
                    //保留 .part，下次续传
                }
                if (retryable_net) throw Error(-2, "网络请求失败: " + msg + " (" + url + ")");
            }
            return resp;
        }

        log_msg("retry " + std::to_string(attempt) + "/" + std::to_string(retries) + " after " +
                std::to_string(backoff_ms) + "ms: http=" + std::to_string(status) + " rc=" +
                std::to_string((int)rc) + " " + url);
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        backoff_ms = std::min<int64_t>(backoff_ms * 2, 8000);
    }
}

HttpResponse HttpClient::get(const std::string& url,
                             const std::map<std::string, std::string>& headers, int retries) {
    return perform(url, "GET", {}, headers, nullptr, false, {}, retries);
}

HttpResponse HttpClient::get_api(const std::string& api,
                                 std::map<std::string, std::string> params) {
    //有全局 mixin key 就签名（nav 拉取过之后）；否则原样请求，
    //少数接口在未签名时也能用，但签名后更稳。
    std::string signed_params = wbi_sign_params(params);
    std::string url = api;
    if (!signed_params.empty()) url += (api.find('?') == std::string::npos ? "?" : "&") + signed_params;
    return perform(url, "GET", {}, {}, nullptr, false, {}, 2);
}

long HttpClient::download_file(const std::string& url, const std::string& dest_path,
                               ProgressFn progress, int retries) {
    //目标目录提前建好；文件不存在时先建空文件
    auto slash = dest_path.find_last_of("/\\");
    if (slash != std::string::npos) ensure_dir(dest_path.substr(0, slash));
    if (!file_exists(dest_path)) {
        write_text_file(dest_path, "");
    }
    HttpResponse r = perform(url, "GET", {}, {}, progress, true, dest_path, retries);
    return r.status;
}

std::string url_query_value(const std::string& url, const std::string& key) {
    auto q = url.find('?');
    if (q == std::string::npos) return {};
    std::string query = url.substr(q + 1);
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        auto eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == key) return pair.substr(eq + 1);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
}

std::string build_query(const std::map<std::string, std::string>& params) {
    std::string out;
    for (const auto& kv : params) {
        if (!out.empty()) out += "&";
        out += url_encode(kv.first) + "=" + url_encode(kv.second);
    }
    return out;
}

}  // namespace bc
