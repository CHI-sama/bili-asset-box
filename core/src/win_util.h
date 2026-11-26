// win_util.h —— Windows 平台专属小工具（DPAPI、注册表代理读取等）
#pragma once

#include <string>

namespace bc {

//用当前用户的 DPAPI 加密/解密任意字节串。失败抛 Error。
std::string dpapi_protect(const std::string& plain);
std::string dpapi_unprotect(const std::string& cipher);

//读取系统代理设置（ProxyEnable / ProxyServer）。没有配置返回空串。
//只读注册表，不修改。
std::string read_system_proxy();

//打开系统默认浏览器（失败返回 false）
bool open_url_in_browser(const std::string& url);

}  // namespace bc
