// dispatch.cpp —— op 字符串到具体实现的映射表
//约定：所有异常在这里被翻译成 Error，真正的异常防火墙在 abi.cpp。
#include <functional>
#include <map>

#include "api.h"
#include "ratelimit.h"
#include "util.h"
#include "win_util.h"

namespace bc {

namespace {
using Handler = std::function<json(const json&)>;

const std::map<std::string, Handler>& table() {
    static const std::map<std::string, Handler> t = {
        //基础 / 登录
        {"nav", api_nav},
        {"wbi_refresh", api_nav},
        {"buvid", api_buvid},
        {"login_qr_generate", api_login_qr_generate},
        {"login_qr_poll", api_login_qr_poll},
        {"login_cookie_set", api_login_cookie_set},
        {"logout", api_logout},
        //搜索与目录
        {"garb_search", api_garb_search},
        {"collection_subjects", api_collection_subjects},
        {"collection_detail", api_collection_detail},
        {"suit_detail", api_suit_detail},
        {"suit_item", api_suit_detail},  //兼容别名
        {"my_assets", api_my_assets},
        //表情
        {"live_emoticons", api_live_emoticons},
        {"emote_panel", api_emote_panel},
        {"emote_package", api_emote_package},
        //视频
        {"video_info", api_video_info},
        {"video_playurl", api_video_playurl},
        //杂项
        {"open_url",
         [](const json& a) -> json {
             std::string url = a.value("url", "");
             if (url.empty()) throw Error(-1, "url 不能为空");
             bool ok = open_url_in_browser(url);
             return json{{"ok", ok}};
         }},
        {"system_proxy",
         [](const json&) -> json { return json{{"proxy", read_system_proxy()}}; }},
        //限速设置：GUI 里改完立即生效，不用重启内核
        {"rate_get",
         [](const json&) -> json {
             return json{{"api_interval_ms", RateLimiter::instance().api_interval_ms()},
                         {"file_interval_ms", RateLimiter::instance().file_interval_ms()},
                         {"per_file_kbps", RateLimiter::instance().per_file_kbps()},
                         {"cooldown_ms", RateLimiter::instance().cool_remaining_ms()}};
         }},
        {"rate_configure",
         [](const json& a) -> json {
             int api_ms = (int)a.value("api_interval_ms", RateLimiter::instance().api_interval_ms());
             int file_ms = (int)a.value("file_interval_ms", RateLimiter::instance().file_interval_ms());
             int kbps = (int)a.value("per_file_kbps", RateLimiter::instance().per_file_kbps());
             RateLimiter::instance().configure(api_ms, file_ms, kbps);
             return json{{"ok", true},
                         {"api_interval_ms", RateLimiter::instance().api_interval_ms()},
                         {"file_interval_ms", RateLimiter::instance().file_interval_ms()},
                         {"per_file_kbps", RateLimiter::instance().per_file_kbps()}};
         }},
        {"feedback_bundle", api_feedback_bundle},
    };
    return t;
}
}  // namespace

json call_op(const std::string& op, const json& args) {
    auto it = table().find(op);
    if (it == table().end()) throw Error(-1, "未知操作: " + op);
    return it->second(args);
}

}  // namespace bc
