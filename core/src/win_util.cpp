// win_util.cpp —— DPAPI / 系统代理 / 打开浏览器
#include "win_util.h"

#include <windows.h>
#include <dpapi.h>
#include <shellapi.h>
#include <winreg.h>

#include <vector>

#include "util.h"

namespace bc {

std::string dpapi_protect(const std::string& plain) {
    DATA_BLOB in{};
    in.pbData = (BYTE*)plain.data();
    in.cbData = (DWORD)plain.size();
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"BiliAssetBox", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        throw Error(-99, "DPAPI 加密失败: " + std::to_string(GetLastError()));
    }
    std::string result((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

std::string dpapi_unprotect(const std::string& cipher) {
    if (cipher.empty()) return {};
    DATA_BLOB in{};
    in.pbData = (BYTE*)cipher.data();
    in.cbData = (DWORD)cipher.size();
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        throw Error(-99, "DPAPI 解密失败: " + std::to_string(GetLastError()));
    }
    std::string result((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

std::string read_system_proxy() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", 0,
                      KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }
    DWORD enable = 0;
    DWORD size = sizeof(enable);
    DWORD type = 0;
    std::string result;
    if (RegQueryValueExW(key, L"ProxyEnable", nullptr, &type, (LPBYTE)&enable, &size) ==
            ERROR_SUCCESS &&
        enable != 0) {
        wchar_t buf[1024] = {0};
        DWORD cb = sizeof(buf) - sizeof(wchar_t);
        if (RegQueryValueExW(key, L"ProxyServer", nullptr, &type, (LPBYTE)buf, &cb) ==
            ERROR_SUCCESS) {
            result = wide_to_utf8(buf);
            // "127.0.0.1:7890" 没有协议前缀时补 http://
            if (!result.empty() && result.find("://") == std::string::npos) {
                result = "http://" + result;
            }
        }
    }
    RegCloseKey(key);
    return result;
}

bool open_url_in_browser(const std::string& url) {
    std::wstring w = utf8_to_wide(url);
    HINSTANCE r = ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

}  // namespace bc
