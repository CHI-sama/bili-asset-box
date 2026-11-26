// api.cpp —— B 站接口实现（查询类）
#include "api.h"

#include <algorithm>
#include <mutex>
#include <set>

#include "util.h"
#include "ratelimit.h"
#include "wbi.h"

namespace bc {

namespace {

ApiContext g_ctx;
std::mutex g_ctx_mutex;

//参数读取小工具
std::string arg_str(const json& a, const char* key, const std::string& def = "") {
    auto it = a.find(key);
    if (it == a.end() || it->is_null()) return def;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<int64_t>());
    return def;
}

int64_t arg_int(const json& a, const char* key, int64_t def = 0) {
    auto it = a.find(key);
    if (it == a.end() || it->is_null()) return def;
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

bool looks_like_url(const std::string& s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0 || s.rfind("//", 0) == 0;
}

//只保留真正是素材的扩展名
bool useful_ext(const std::string& ext) {
    static const std::set<std::string> ok = {".png",  ".jpg", ".jpeg", ".webp", ".gif",
                                             ".mp4",  ".webm", ".json", ".bin",  ".zip",
                                             ".apng", ".avif", ".svg", ".mp3",  ".m4a"};
    return ok.count(ext) > 0;
}

std::string kind_of(const std::string& ext) {
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".gif" ||
        ext == ".apng" || ext == ".avif" || ext == ".svg")
        return "image";
    if (ext == ".mp4" || ext == ".webm") return "video";
    if (ext == ".zip") return "archive";
    if (ext == ".json") return "json";
    if (ext == ".mp3" || ext == ".m4a") return "audio";
    return "file";
}

}  // namespace

ApiContext& ctx() { return g_ctx; }

json check_bili(const HttpResponse& r, const std::string& what) {
    if (r.status == 0) throw Error(-2, what + ": 无响应");
    if (r.status == 412) throw Error(-2, what + ": 被风控拦截(412)，请降低频率或配置代理");
    if (r.status >= 400 && r.status < 500 && r.status != 429) {
        //让调用方看到具体状态码，但 401/403 通常意味着 Cookie 失效
        if (r.status == 401 || r.status == 403) throw Error(-4, what + ": 登录状态无效");
    }
    json j;
    try {
        j = json::parse(r.body);
    } catch (...) {
        throw Error(-3, what + ": 响应不是 JSON (HTTP " + std::to_string(r.status) + ")");
    }
    int64_t code = ival(j, "code", 0);
    if (code != 0) {
        std::string msg = sval(j, "message");
        if (code == -101 || (code == -400 && msg.find("登录") != std::string::npos)) {
            throw Error(-4, what + ": " + (msg.empty() ? "账号未登录" : msg));
        }
        if (code == -352) {
            // WBI 校验失败基本等于“请求太密/签名不对”，先全局歇 60 秒再让用户重试
            RateLimiter::instance().cool_down(60000, "接口返回 -352");
            throw Error(-2, what + ": 触发 WBI 校验(-352)，已暂停 60 秒，请降低频率后重试");
        }
        if (code == -412) {
            RateLimiter::instance().cool_down(60000, "接口返回 -412");
            throw Error(-2, what + ": 请求被风控(-412)，已暂停 60 秒，请稍后再试");
        }
        throw Error(-3, what + ": 接口返回 " + std::to_string(code) + " " + msg);
    }
    if (j.contains("data")) return j["data"];
    return json::object();
}

//nav
json api_nav(const json&) {
    auto r = g_ctx.http.get("https://api.bilibili.com/x/web-interface/nav");
    json data;
    try {
        data = check_bili(r, "nav");
    } catch (const Error& e) {
        //未登录时 nav 返回 code=-101，但仍带 wbi_img，可以继续用
        json j = json::parse(r.body);
        if (j.contains("data")) data = j["data"];
        if (e.code != -4) throw;
    }
    if (data.contains("wbi_img")) {
        std::string img = sval(data["wbi_img"], "img_url");
        std::string sub = sval(data["wbi_img"], "sub_url");
        wbi_set_keys_from_nav(img, sub);
    }
    g_ctx.logged_in = data.value("isLogin", false);
    g_ctx.mid = ival(data, "mid");
    g_ctx.uname = sval(data, "uname");
    g_ctx.face = sval(data, "face");
    json out = {
        {"logged_in", g_ctx.logged_in},
        {"mid", std::to_string(g_ctx.mid)},
        {"uname", g_ctx.uname},
        {"face", g_ctx.face},
        {"wbi_key", wbi_current_key()},
        {"csrf", g_ctx.http.cookie_value("bili_jct")},
        {"cookie_present", !g_ctx.http.cookie_string().empty()},
    };
    return out;
}

//buvid
//让服务端认为我们是正常浏览器；同时避免匿名请求被限流
json api_buvid(const json&) {
    auto r = g_ctx.http.get("https://api.bilibili.com/x/frontend/finger/spi");
    json data = check_bili(r, "buvid");
    std::string b3 = sval(data, "b_3");
    std::string b4 = sval(data, "b_4");
    //写进 cookie 罐子，后续请求自动携带
    std::string cookies = g_ctx.http.cookie_string();
    std::map<std::string, std::string> m;
    if (!b3.empty()) m["buvid3"] = b3;
    if (!b4.empty()) m["buvid4"] = b4;
    for (auto& kv : m) {
        if (cookies.find(kv.first + "=") == std::string::npos) {
            if (!cookies.empty()) cookies += "; ";
            cookies += kv.first + "=" + kv.second;
        }
    }
    g_ctx.http.set_cookie_string(cookies);
    return {{"buvid3", b3}, {"buvid4", b4}};
}

//装扮/收藏集搜索
json api_garb_search(const json& args) {
    std::string keyword = arg_str(args, "keyword");
    if (keyword.empty()) throw Error(-1, "搜索关键词不能为空");
    int64_t pn = std::max<int64_t>(1, arg_int(args, "pn", 1));
    int64_t ps = std::min<int64_t>(50, std::max<int64_t>(1, arg_int(args, "ps", 20)));
    auto r = g_ctx.http.get_api("https://api.bilibili.com/x/garb/v2/mall/home/search",
                                {{"key_word", keyword},
                                 {"pn", std::to_string(pn)},
                                 {"ps", std::to_string(ps)}});
    json data = check_bili(r, "搜索装扮");
    json items = json::array();
    const json& list = data.contains("list") ? data["list"] : json::array();
    for (const auto& it : list) {
        json props = it.value("properties", json::object());
        json entry;
        entry["name"] = sval(it, "name");
        entry["cover"] = sval(props, "image_cover");
        entry["price"] = ival(props, "sale_price");
        if (sval(props, "type") == "dlc_act") {
            entry["kind"] = "collection";
            entry["act_id"] = ival(props, "dlc_act_id");
            entry["lottery_id"] = ival(props, "dlc_lottery_id");
            entry["lottery_type"] = ival(props, "dlc_lottery_type");
            entry["item_id"] = 0;
        } else {
            entry["kind"] = "suit";
            entry["item_id"] = ival(props, "item_id");
            entry["card_item_id"] = ival(props, "item_id_card");
            entry["emoji_item_id"] = ival(props, "item_id_emoji_package");
            entry["act_id"] = 0;
            entry["lottery_id"] = 0;
        }
        items.push_back(entry);
    }
    return {{"items", items},
            {"total", ival(data, "total")},
            {"pn", pn},
            {"ps", ps}};
}

//收藏集目录
json api_collection_subjects(const json& args) {
    int64_t subject_id = arg_int(args, "subject_id", 42);
    auto r = g_ctx.http.get_api("https://api.bilibili.com/x/garb/card/subject/list",
                                {{"subject_id", std::to_string(subject_id)}});
    json data = check_bili(r, "收藏集目录");
    json cards = json::array();
    for (const auto& c : data.value("subject_card_list", json::array())) {
        cards.push_back({
            {"act_id", ival(c, "act_id")},
            {"name", sval(c, "act_name")},
            {"cover", sval(c, "act_pic")},
            {"price", ival(c, "sale_price")},
            {"link", sval(c, "act_link")},
        });
    }
    return {{"subject_id", subject_id},
            {"subject_name", sval(data, "subject_name")},
            {"cards", cards}};
}

//收藏集详情
json api_collection_detail(const json& args) {
    int64_t act_id = arg_int(args, "act_id");
    int64_t lottery_id = arg_int(args, "lottery_id");
    if (act_id <= 0) throw Error(-1, "act_id 无效");
    auto r = g_ctx.http.get_api("https://api.bilibili.com/x/vas/dlc_act/lottery_home_detail",
                                {{"act_id", std::to_string(act_id)},
                                 {"lottery_id", std::to_string(lottery_id)}});
    json data = check_bili(r, "收藏集详情");
    json cards = json::array();
    for (const auto& it : data.value("item_list", json::array())) {
        json ci = it.value("card_info", json::object());
        json videos = json::array();
        for (const auto& v : ci.value("video_list", json::array())) {
            std::string u = sval(v, "video_url");
            if (u.empty()) u = sval(v, "url");
            if (!u.empty()) videos.push_back(normalize_url(u));
        }
        std::string img = sval(ci, "card_img");
        cards.push_back({
            {"card_id", ival(ci, "card_id")},
            {"name", sval(ci, "card_name")},
            {"img", normalize_url(strip_hdslb_suffix(img))},
            {"width", ival(ci, "width")},
            {"height", ival(ci, "height")},
            {"scarcity", ival(ci, "card_scarcity")},
            {"videos", videos},
            {"is_dynamic", !videos.empty()},
        });
    }
    //这个接口的标题字段叫 name，旧版脚本里写的是 act_name，两个都兜一下
    std::string act_name = sval(data, "act_name");
    if (act_name.empty()) act_name = sval(data, "name");
    return {{"act_id", act_id},
            {"lottery_id", lottery_id},
            {"name", act_name},
            {"total", (int64_t)cards.size()},
            {"cards", cards}};
}

//装扮详情
void harvest_urls(const json& node, const std::string& key_hint, json& out_urls) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            harvest_urls(it.value(), it.key(), out_urls);
        }
    } else if (node.is_array()) {
        for (const auto& v : node) harvest_urls(v, key_hint, out_urls);
    } else if (node.is_string()) {
        std::string s = node.get<std::string>();
        if (!looks_like_url(s)) return;
        //只收素材类字段
        static const char* hints[] = {"image", "url", "icon", "cover", "src", "download", "file"};
        bool ok = false;
        for (const char* h : hints) {
            if (key_hint.find(h) != std::string::npos) {
                ok = true;
                break;
            }
        }
        if (!ok) return;
        std::string url = normalize_url(strip_hdslb_suffix(s));
        std::string ext = file_ext(url);
        if (!useful_ext(ext)) return;
        out_urls.push_back(url);
    }
}

namespace {

//把单个 suit item 转成文件列表
void append_item_files(const json& item, const std::string& part, int64_t item_id, json& files,
                       std::set<std::string>& seen) {
    std::string item_name = sval(item, "name");
    if (item_name.empty()) item_name = sval(item, "item_name");
    if (item_name.empty()) item_name = part;
    json props = item.value("properties", json::object());
    json urls = json::array();
    harvest_urls(props, part, urls);
    //有些接口用 image_urls 直接给数组
    if (item.contains("image_urls")) harvest_urls(item["image_urls"], "image", urls);
    if (item.contains("image")) harvest_urls(item["image"], "image", urls);
    int idx = 0;
    for (const auto& u : urls) {
        std::string url = u.get<std::string>();
        if (!seen.insert(url).second) continue;
        std::string ext = file_ext(url);
        std::string base = sanitize_filename(part + "_" + item_name, 80);
        if (idx > 0) base += "_" + std::to_string(idx);
        if (ext.empty()) ext = ".bin";
        files.push_back({{"url", url},
                         {"name", base + ext},
                         {"part", part},
                         {"item_id", item_id},
                         {"kind", kind_of(ext)}});
        ++idx;
    }
}

}  // namespace

json flatten_suit(const json& data, int64_t item_id) {
    json parts = json::array();
    json files = json::array();
    std::set<std::string> seen;

    json groups = data.contains("suit_items") ? data["suit_items"] : json::object();
    std::string suit_name = sval(data, "name");
    if (suit_name.empty()) suit_name = sval(data, "suit_name");

    if (groups.is_object()) {
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            const std::string& part = it.key();
            const json& node = it.value();
            json part_files = json::array();
            if (node.is_object() && node.contains("items") && node["items"].is_array()) {
                for (const auto& item : node["items"]) {
                    append_item_files(item, part, ival(item, "item_id"), part_files, seen);
                }
            } else if (node.is_array()) {
                for (const auto& item : node) {
                    append_item_files(item, part, ival(item, "item_id"), part_files, seen);
                }
            } else if (node.is_object()) {
                append_item_files(node, part, ival(node, "item_id"), part_files, seen);
            }
            for (const auto& f : part_files) files.push_back(f);
            parts.push_back({{"part", part}, {"count", (int64_t)part_files.size()}});
        }
    }
    return {{"item_id", item_id},
            {"name", suit_name},
            {"parts", parts},
            {"files", files},
            {"count", (int64_t)files.size()}};
}

json api_suit_detail(const json& args) {
    int64_t item_id = arg_int(args, "item_id");
    if (item_id <= 0) throw Error(-1, "item_id 无效");
    auto r = g_ctx.http.get_api("https://api.bilibili.com/x/garb/v2/mall/suit/detail",
                                {{"item_id", std::to_string(item_id)},
                                 {"part", "suit"},
                                 {"from", ""},
                                 {"from_id", ""}});
    json data = check_bili(r, "装扮详情");
    if (!data.contains("suit_items") || data["suit_items"].empty()) {
        //回退：老接口返回扁平数组
        auto r2 = g_ctx.http.get_api("https://api.bilibili.com/x/garb/mall/item/suit",
                                     {{"item_id", std::to_string(item_id)}});
        json d2 = check_bili(r2, "装扮详情(备用)");
        json files = json::array();
        std::set<std::string> seen;
        for (const auto& it : d2.value("suit_items", json::array())) {
            append_item_files(it, sval(it, "part"), ival(it, "id"), files, seen);
        }
        return {{"item_id", item_id},
                {"name", sval(d2, "name")},
                {"parts", json::array()},
                {"files", files},
                {"count", (int64_t)files.size()}};
    }
    return flatten_suit(data, item_id);
}

//我拥有的资产
json api_my_assets(const json&) {
    json out = {{"suits", json::array()}, {"collections", json::array()}, {"raw", json::object()}};
    std::set<int64_t> seen_suits;
    std::set<int64_t> seen_acts;

    //递归收集：同时兼容多个版本的返回结构
    std::function<void(const json&)> walk = [&](const json& node) {
        if (node.is_object()) {
            int64_t item_id = ival(node, "item_id");
            int64_t suit_id = ival(node, "suit_id");
            if ((item_id > 0 || suit_id > 0) &&
                (node.contains("name") || node.contains("suit_name") || node.contains("item_name"))) {
                int64_t id = item_id > 0 ? item_id : suit_id;
                if (seen_suits.insert(id).second) {
                    out["suits"].push_back({
                        {"item_id", id},
                        {"name", sval(node, "suit_name", sval(node, "name", sval(node, "item_name")))},
                        {"cover", normalize_url(strip_hdslb_suffix(
                                      sval(node, "image_cover", sval(node, "cover", sval(node, "image")))))},
                        {"expire", sval(node, "expire")},
                    });
                }
            }
            //收藏集卡片
            int64_t act_id = ival(node, "act_id");
            if (act_id > 0 && node.contains("card_number")) {
                if (seen_acts.insert(act_id * 1000 + ival(node, "card_id")) .second) {
                    out["collections"].push_back({
                        {"act_id", act_id},
                        {"card_id", ival(node, "card_id")},
                        {"name", sval(node, "card_name", sval(node, "name"))},
                        {"number", sval(node, "card_number")},
                        {"image", normalize_url(strip_hdslb_suffix(sval(node, "card_img")))},
                    });
                }
            }
            for (auto it = node.begin(); it != node.end(); ++it) walk(it.value());
        } else if (node.is_array()) {
            for (const auto& v : node) walk(v);
        }
    };

    const char* endpoints[] = {"https://api.bilibili.com/x/garb/v3/user/asset",
                               "https://api.bilibili.com/x/garb/user/suit/asset",
                               "https://api.bilibili.com/x/vas/user/dlc/card/number/list"};
    for (const char* ep : endpoints) {
        try {
            auto r = g_ctx.http.get_api(ep, {});
            json data = check_bili(r, "我的资产");
            std::string key = ep;
            out["raw"][key] = data;
            walk(data);
        } catch (const Error& e) {
            if (e.code == -4) throw;  //登录态问题直接上报
            log_msg(std::string("my_assets 忽略失败接口: ") + ep + " -> " + e.what());
        }
    }
    return out;
}

//直播表情
json api_live_emoticons(const json& args) {
    std::string room = arg_str(args, "room_id");
    if (room.empty()) throw Error(-1, "room_id 不能为空");
    auto r = g_ctx.http.get("https://api.live.bilibili.com/xlive/web-ucenter/v2/emoticon/GetEmoticons?platform=pc&room_id=" +
                            url_encode(room));
    json data = check_bili(r, "直播间表情");
    json packages = json::array();
    for (const auto& p : data.value("data", json::array())) {
        json emotes = json::array();
        for (const auto& e : p.value("emoticons", json::array())) {
            std::string url = normalize_url(strip_hdslb_suffix(sval(e, "url")));
            emotes.push_back({
                {"id", ival(e, "emoticon_id")},
                {"text", sval(e, "emoji")},
                {"unique", sval(e, "emoticon_unique")},
                {"url", url},
                {"width", ival(e, "width")},
                {"height", ival(e, "height")},
                {"dynamic", ival(e, "is_dynamic") != 0},
            });
        }
        packages.push_back({
            {"pkg_id", ival(p, "pkg_id")},
            {"name", sval(p, "pkg_name")},
            {"desc", sval(p, "pkg_descript")},
            {"cover", normalize_url(strip_hdslb_suffix(sval(p, "current_cover")))},
            {"emoticons", emotes},
        });
    }
    return {{"room_id", room}, {"packages", packages}};
}

//评论表情 / 装扮表情
json flatten_emote_packages(const json& data) {
    json packages = json::array();
    //面板接口：data.packages[] 每包带 emotes[]
    for (const auto& p : data.value("packages", json::array())) {
        json list = json::array();
        for (const auto& e : p.value("emote", json::array())) {
            json meta = e.value("meta", json::object());
            list.push_back({
                {"id", ival(e, "id")},
                {"text", sval(e, "text")},
                {"url", normalize_url(strip_hdslb_suffix(sval(e, "url")))},
                {"size", ival(meta, "size")},
                {"item_id", ival(meta, "item_id")},
                {"gif", sval(e, "url").find(".gif") != std::string::npos},
            });
        }
        packages.push_back({{"pkg_id", ival(p, "pkg_id")},
                            {"name", sval(p, "text")},
                            {"emoticons", list}});
    }
    return {{"packages", packages}};
}

json api_emote_panel(const json& args) {
    std::string business = arg_str(args, "business", "reply");
    auto r = g_ctx.http.get_api("https://api.bilibili.com/x/emote/user/panel/web",
                                {{"business", business}});
    json data = check_bili(r, "表情面板");
    json out = flatten_emote_packages(data);
    out["business"] = business;
    return out;
}

json api_emote_package(const json& args) {
    std::string business = arg_str(args, "business", "reply");
    std::string ids = arg_str(args, "ids");
    if (ids.empty()) throw Error(-1, "ids 不能为空");
    auto r = g_ctx.http.get_api("https://api.bilibili.com/x/emote/package",
                                {{"business", business}, {"ids", ids}});
    json data = check_bili(r, "表情包详情");
    //返回结构是 packages 数组，复用直播表情的解析
    json packages = json::array();
    for (const auto& p : data.value("packages", json::array())) {
        json list = json::array();
        for (const auto& e : p.value("emoticons", json::array())) {
            list.push_back({{"id", ival(e, "emoticon_id")},
                            {"text", sval(e, "emoji")},
                            {"url", normalize_url(strip_hdslb_suffix(sval(e, "url")))},
                            {"width", ival(e, "width")},
                            {"height", ival(e, "height")}});
        }
        packages.push_back({{"pkg_id", ival(p, "pkg_id")},
                            {"name", sval(p, "pkg_name")},
                            {"emoticons", list}});
    }
    return {{"business", business}, {"packages", packages}};
}

//视频
json api_video_info(const json& args) {
    std::string bvid = arg_str(args, "bvid");
    int64_t aid = arg_int(args, "aid");
    std::string input = arg_str(args, "input");
    //支持直接粘贴 BV 号 / av 号 / 完整链接
    if (bvid.empty() && aid <= 0 && !input.empty()) {
        auto pos = input.find("BV");
        if (pos != std::string::npos && input.size() >= pos + 12) {
            bvid = input.substr(pos, 12);
        } else {
            auto avp = input.find("av");
            if (avp != std::string::npos) {
                try {
                    aid = std::stoll(input.substr(avp + 2));
                } catch (...) {
                }
            }
        }
    }
    std::map<std::string, std::string> params;
    if (!bvid.empty()) params["bvid"] = bvid;
    else if (aid > 0) params["aid"] = std::to_string(aid);
    else throw Error(-1, "请提供 bvid 或 aid");
    auto r = g_ctx.http.get_api("https://api.bilibili.com/x/web-interface/view", params);
    json data = check_bili(r, "视频信息");
    json pages = json::array();
    for (const auto& p : data.value("pages", json::array())) {
        pages.push_back({{"cid", ival(p, "cid")},
                         {"page", ival(p, "page")},
                         {"title", sval(p, "part")},
                         {"duration", ival(p, "duration")}});
    }
    return {{"bvid", sval(data, "bvid")},
            {"aid", ival(data, "aid")},
            {"title", sval(data, "title")},
            {"cover", normalize_url(strip_hdslb_suffix(sval(data, "pic")))},
            {"duration", ival(data, "duration")},
            {"owner", sval(data.value("owner", json::object()), "name")},
            {"desc", sval(data, "desc")},
            {"pages", pages}};
}

json api_video_playurl(const json& args) {
    std::string bvid = arg_str(args, "bvid");
    int64_t cid = arg_int(args, "cid");
    int64_t qn = arg_int(args, "qn", 80);
    if (bvid.empty() || cid <= 0) throw Error(-1, "bvid 和 cid 必须同时提供");
    auto r = g_ctx.http.get_api(
        "https://api.bilibili.com/x/player/wbi/playurl",
        {{"bvid", bvid},
         {"cid", std::to_string(cid)},
         {"qn", std::to_string(qn)},
         {"fnval", "4048"},
         {"fourk", "1"}});
    json data = check_bili(r, "取视频流");
    json videos = json::array();
    json audios = json::array();
    int64_t duration = ival(data, "timelength") / 1000;
    if (data.contains("dash")) {
        const json& dash = data["dash"];
        duration = ival(dash, "duration", duration);
        for (const auto& v : dash.value("video", json::array())) {
            std::string base = sval(v, "base_url");
            if (base.empty()) base = sval(v, "baseUrl");
            json backups = json::array();
            for (const auto& b : v.value("backup_url", json::array())) backups.push_back(b);
            videos.push_back({{"id", ival(v, "id")},
                              {"quality", ival(v, "id")},
                              {"codecs", sval(v, "codecs")},
                              {"width", ival(v, "width")},
                              {"height", ival(v, "height")},
                              {"bandwidth", ival(v, "bandwidth")},
                              {"url", base},
                              {"backups", backups}});
        }
        for (const auto& a : dash.value("audio", json::array())) {
            std::string base = sval(a, "base_url");
            if (base.empty()) base = sval(a, "baseUrl");
            audios.push_back({{"id", ival(a, "id")},
                              {"bandwidth", ival(a, "bandwidth")},
                              {"url", base}});
        }
    } else if (data.contains("durl")) {
        //老格式（flv/mp4 整段）
        for (const auto& d : data["durl"]) {
            videos.push_back({{"id", ival(data, "quality", qn)},
                              {"quality", ival(data, "quality", qn)},
                              {"codecs", "durl"},
                              {"width", 0},
                              {"height", 0},
                              {"bandwidth", 0},
                              {"url", sval(d, "url")},
                              {"backups", d.value("backup_url", json::array())}});
        }
    }
    //清晰度从高到低排序，便于 GUI 选最高
    std::sort(videos.begin(), videos.end(), [](const json& a, const json& b) {
        return a.value("quality", 0) > b.value("quality", 0);
    });
    json accept = data.value("accept_quality", json::array());
    return {{"bvid", bvid},
            {"cid", cid},
            {"duration", duration},
            {"accept_quality", accept},
            {"videos", videos},
            {"audios", audios}};
}

}  // namespace bc
