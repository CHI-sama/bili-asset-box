// wbi.cpp —— WBI 签名实现
#include "wbi.h"

#include <algorithm>
#include <ctime>
#include <mutex>
#include <vector>

#include "util.h"

namespace bc {

namespace {

//官方前端混淆表（bilibili-API-collect 文档内给出的 64 项）
const int kMixinKeyEncTab[64] = {
    46, 47, 18, 2,  53, 8,  23, 32, 15, 50, 10, 31, 58, 3,  45, 35,
    27, 43, 5,  49, 33, 9,  42, 19, 29, 28, 14, 39, 12, 38, 41, 13,
    37, 48, 7,  16, 24, 55, 40, 61, 26, 17, 0,  1,  60, 51, 30, 4,
    22, 25, 54, 21, 56, 59, 6,  63, 57, 62, 11, 36, 20, 34, 44, 52};

std::mutex g_key_mutex;
std::string g_mixin_key;

//取 URL 最后一段并去掉扩展名：.../7cd084941338484aae1ad9425b84077c.png -> 7cd0...
std::string key_from_url(const std::string& url) {
    std::string name = filename_from_url(url);
    auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

// JS encodeURIComponent 不转义的字符里，b 站签名还要求过滤 !'()*
std::string sanitize_param_value(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        if (c == '!' || c == '\'' || c == '(' || c == ')' || c == '*') continue;
        out.push_back(c);
    }
    return out;
}

}  // namespace

std::string wbi_mixin_key(const std::string& img_key, const std::string& sub_key) {
    std::string raw = img_key + sub_key;
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 32 && i < 64; ++i) {
        int idx = kMixinKeyEncTab[i];
        if (idx >= 0 && idx < (int)raw.size()) out.push_back(raw[(size_t)idx]);
    }
    return out;
}

bool wbi_set_keys_from_nav(const std::string& img_url, const std::string& sub_url) {
    std::string img = key_from_url(img_url);
    std::string sub = key_from_url(sub_url);
    if (img.empty() || sub.empty()) return false;
    std::string key = wbi_mixin_key(img, sub);
    if (key.size() < 32) return false;
    std::lock_guard<std::mutex> lk(g_key_mutex);
    g_mixin_key = key;
    return true;
}

std::string wbi_current_key() {
    std::lock_guard<std::mutex> lk(g_key_mutex);
    return g_mixin_key;
}

std::string wbi_sign_with_key(std::map<std::string, std::string> params,
                              const std::string& mixin_key, int64_t wts) {
    params["wts"] = std::to_string(wts);
    std::string query;
    for (const auto& kv : params) {  // std::map 已按 key 升序
        if (!query.empty()) query += "&";
        query += url_encode(kv.first) + "=" + url_encode(sanitize_param_value(kv.second));
    }
    std::string w_rid = md5_hex(query + mixin_key);
    return query + "&w_rid=" + w_rid;
}

std::string wbi_sign_params(std::map<std::string, std::string> params) {
    std::string key = wbi_current_key();
    if (key.empty()) return {};
    int64_t wts = (int64_t)std::time(nullptr);
    return wbi_sign_with_key(std::move(params), key, wts);
}

}  // namespace bc
