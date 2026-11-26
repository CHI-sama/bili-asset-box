// wbi.h —— B 站 WBI 签名
//
//算法来源：bilibili-API-collect/docs/misc/sign/wbi.md
//   1. 从 nav 接口的 wbi_img.img_url / sub_url 里取文件名（去掉扩展名）
//   2. img_key + sub_key 按 MIXIN_KEY_ENC_TAB 重排，取前 32 位 = mixin_key
//   3. 参数加 wts（秒），按 key 升序，值里过滤 !'()*，再 encodeURIComponent
//   4. md5(query + mixin_key) = w_rid
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace bc {

//用 img_key / sub_key 计算 mixin_key
std::string wbi_mixin_key(const std::string& img_key, const std::string& sub_key);

//由 nav 返回的 wbi_img 结构刷新全局 mixin_key；返回是否成功
bool wbi_set_keys_from_nav(const std::string& img_url, const std::string& sub_url);

//全局 mixin_key（未初始化时为空）
std::string wbi_current_key();

//给参数加 wts + w_rid，返回 "a=1&b=2&wts=...&w_rid=..." 形式的 query。
//未初始化 mixin_key 时返回空串（调用方应保持原样请求）。
std::string wbi_sign_params(std::map<std::string, std::string> params);

//供单元测试使用：直接按给定 key 签名，不依赖全局状态
std::string wbi_sign_with_key(std::map<std::string, std::string> params,
                              const std::string& mixin_key, int64_t wts);

}  // namespace bc
