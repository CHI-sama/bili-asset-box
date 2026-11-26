// api.h —— B 站接口封装：每个 op 接收 JSON 参数、返回 JSON 结果
#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "http.h"

namespace bc {

using json = nlohmann::json;

// API 层上下文：一个 HttpClient + 一些全局配置
struct ApiContext {
    HttpClient http;
    std::string data_dir;
    std::string cache_dir;
    std::string ca_bundle;
    std::string proxy;
    bool logged_in = false;
    int64_t mid = 0;
    std::string uname;
    std::string face;
};

ApiContext& ctx();

//统一入口：op + 参数 -> 结果；出错抛 Error
json call_op(const std::string& op, const json& args);

//具体实现（供 call_op 分发，也可被其他模块直接调用）
json api_nav(const json& args);
json api_buvid(const json& args);
json api_garb_search(const json& args);
json api_collection_subjects(const json& args);
json api_collection_detail(const json& args);
json api_suit_detail(const json& args);
json api_my_assets(const json& args);
json api_live_emoticons(const json& args);
json api_emote_panel(const json& args);
json api_emote_package(const json& args);
json api_video_info(const json& args);
json api_video_playurl(const json& args);
json api_feedback_bundle(const json& args);

//登录相关
json api_login_qr_generate(const json& args);
json api_login_qr_poll(const json& args);
json api_login_cookie_set(const json& args);
json api_logout(const json& args);

// ---- 以下为可被下载任务复用的辅助函数 ----

//校验 B 站标准响应：HTTP 2xx 且 JSON code==0；否则抛 Error（-4 表示未登录）
json check_bili(const HttpResponse& r, const std::string& what);

//把 suit/detail 的原始结构拍平成可直接下载的文件清单
//输出：[{"url","name","part","kind"}]
json flatten_suit(const json& data, int64_t item_id);

//从 properties 里递归收集看起来像 URL 的字段
void harvest_urls(const json& node, const std::string& key_hint, json& out_urls);

//表情包（评论/直播/装扮通用）拍平
json flatten_emote_packages(const json& data);

}  // namespace bc
