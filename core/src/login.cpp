// login.cpp —— 扫码登录、Cookie 保存（DPAPI）、登出
#include <algorithm>
#include <map>

#include "api.h"
#include "login.h"
#include "util.h"
#include "win_util.h"

namespace bc {

namespace {

//本文件自用的小工具（api.cpp 里的同名函数是内部链接，这里需要各自一份）
std::string arg_str(const json& a, const char* key, const std::string& def = "") {
    auto it = a.find(key);
    if (it == a.end() || it->is_null()) return def;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<int64_t>());
    return def;
}

std::string sval(const json& node, const char* key, const std::string& def = "") {
    auto it = node.find(key);
    if (it == node.end() || it->is_null()) return def;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<int64_t>());
    if (it->is_number_float()) return std::to_string(it->get<double>());
    return def;
}

int64_t ival(const json& node, const char* key, int64_t def = 0) {
    auto it = node.find(key);
    if (it == node.end() || it->is_null()) return def;
    if (it->is_number_integer()) return it->get<int64_t>();
    if (it->is_string()) {
        try {
            return std::stoll(it->get<std::string>());
        } catch (...) {
            return def;
        }
    }
    return def;
}

std::string session_file() {
    std::string dir = ctx().data_dir;
    if (dir.empty()) dir = ".";
    ensure_dir(dir);
    return path_join(dir, "session.bin");
}

//从形如 "https://passport.biligame.com/x/passport-login/web/crossDomain?SESSDATA=..&bili_jct=.."
//的 URL 里抽出 cookie 键值对
std::string cookies_from_redirect(const std::string& url) {
    std::map<std::string, std::string> kv;
    auto q = url.find('?');
    if (q == std::string::npos) return {};
    std::string query = url.substr(q + 1);
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            kv[pair.substr(0, eq)] = pair.substr(eq + 1);
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    static const char* need[] = {"SESSDATA", "bili_jct", "DedeUserID", "DedeUserID__ckMd5", "sid"};
    std::string out;
    for (const char* k : need) {
        auto it = kv.find(k);
        if (it == kv.end() || it->second.empty()) continue;
        if (!out.empty()) out += "; ";
        out += std::string(k) + "=" + it->second;
    }
    return out;
}

}  // namespace

//把当前 Cookie 加密落盘；声明在 login.h，ABI 层与 GUI 都可以复用
void persist_session() {
    std::string cookies = ctx().http.cookie_string();
    if (cookies.empty()) return;
    try {
        std::string blob = dpapi_protect(cookies);
        std::string data = b64_encode(blob);
        write_text_file(session_file(), data);
        log_msg("登录状态已加密保存到 " + session_file());
    } catch (const std::exception& e) {
        log_msg(std::string("保存登录状态失败: ") + e.what());
    }
}

//从磁盘恢复登录态（bc_init 时调用）
void load_session_from_disk() {
    std::string path = session_file();
    if (!file_exists(path)) return;
    try {
        std::string b64 = read_text_file(path);
        std::string blob = b64_decode(b64);
        std::string cookies = dpapi_unprotect(blob);
        if (!cookies.empty()) {
            ctx().http.set_cookie_string(cookies);
            log_msg("已恢复登录状态");
        }
    } catch (const std::exception& e) {
        log_msg(std::string("恢复登录状态失败: ") + e.what());
    }
}

json api_login_qr_generate(const json&) {
    auto r = ctx().http.get(
        "https://passport.bilibili.com/x/passport-login/web/qrcode/generate");
    json data = check_bili(r, "生成登录二维码");
    return {{"url", sval(data, "url")}, {"qrcode_key", sval(data, "qrcode_key")}};
}

json api_login_qr_poll(const json& args) {
    std::string key = arg_str(args, "qrcode_key");
    if (key.empty()) throw Error(-1, "qrcode_key 不能为空");
    auto r = ctx().http.get(
        "https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=" +
        url_encode(key));
    json j = json::parse(r.body);
    int64_t code = ival(j, "code", -1);
    if (code != 0) throw Error(-3, "扫码轮询失败: " + sval(j, "message"));
    json data = j.value("data", json::object());
    int64_t state = ival(data, "code", -1);  // 0 成功 / 86101 未扫码 / 86090 已扫码 / 86038 失效
    json out = {{"state", state},
                {"message", sval(data, "message")},
                {"cookie", ""},
                {"logged_in", false}};
    if (state == 0) {
        std::string from_url = cookies_from_redirect(sval(data, "url"));
        if (!from_url.empty()) {
            //合并进现有 cookie
            std::string merged = ctx().http.cookie_string();
            ctx().http.set_cookie_string(from_url);
            //把原有 buvid 等补回去（set_cookie_string 会清空）
            if (!merged.empty()) {
                std::map<std::string, std::string> keep;
                size_t p = 0;
                while (p < merged.size()) {
                    size_t semi = merged.find(';', p);
                    std::string part = merged.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
                    auto eq = part.find('=');
                    if (eq != std::string::npos) {
                        std::string k = trim(part.substr(0, eq));
                        if (k != "SESSDATA" && ctx().http.cookie_value(k).empty()) {
                            keep[k] = trim(part.substr(eq + 1));
                        }
                    }
                    if (semi == std::string::npos) break;
                    p = semi + 1;
                }
                std::string cs = ctx().http.cookie_string();
                for (auto& kv : keep) cs += "; " + kv.first + "=" + kv.second;
                ctx().http.set_cookie_string(cs);
            }
        }
        persist_session();
        json nav;
        try {
            nav = api_nav(json::object());
        } catch (const std::exception& e) {
            log_msg(std::string("登录后刷新 nav 失败: ") + e.what());
        }
        out["cookie"] = ctx().http.cookie_string();
        out["logged_in"] = ctx().logged_in;
        out["nav"] = nav;
        log_msg("扫码登录成功");
    }
    return out;
}

json api_login_cookie_set(const json& args) {
    std::string cookies = arg_str(args, "cookie");
    if (cookies.empty()) throw Error(-1, "Cookie 不能为空");
    //常见粘贴形式：“SESSDATA=xxx; bili_jct=yyy” 或整段 header
    auto pos = cookies.find("Cookie:");
    if (pos != std::string::npos) cookies = cookies.substr(pos + 7);
    size_t start = cookies.find_first_not_of(" \r\n\t");
    if (start != std::string::npos) cookies = cookies.substr(start);
    if (cookies.find("SESSDATA=") == std::string::npos) {
        throw Error(-1, "Cookie 里没有找到 SESSDATA，请复制完整的 Cookie 字符串");
    }
    ctx().http.set_cookie_string(cookies);
    json nav = api_nav(json::object());
    if (!ctx().logged_in) {
        throw Error(-4, "Cookie 无效或已过期，请重新获取");
    }
    persist_session();
    return nav;
}

json api_logout(const json&) {
    ctx().http.clear_cookies();
    ctx().logged_in = false;
    ctx().mid = 0;
    ctx().uname.clear();
    ctx().face.clear();
    std::string path = session_file();
    if (file_exists(path)) remove_file_quiet(path);
    return {{"ok", true}};
}

}  // namespace bc
