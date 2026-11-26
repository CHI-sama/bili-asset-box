// feedback.cpp —— 反馈诊断包：收集环境信息 + 日志，脱敏后打成一个 zip
//注意：这里不联网、不自动上传，只负责在本地生成文件；发送由用户自己在浏览器里完成。
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "api.h"
#include "util.h"
#include "win_util.h"

#ifdef _WIN32
#  include <windows.h>
#endif

namespace fs = std::filesystem;

namespace bc {

namespace {

// ---- 极简 ZIP 写入（只用 store 不压缩，够用且无需 zlib） ----
struct ZipItem {
    std::string name;  // zip 内部路径，统一用 /
    std::string data;
};

uint32_t crc32_of(const std::string& data) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char ch : data) crc = table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void put16(std::string& out, uint16_t v) {
    out.push_back((char)(v & 0xFF));
    out.push_back((char)((v >> 8) & 0xFF));
}
void put32(std::string& out, uint32_t v) {
    put16(out, (uint16_t)(v & 0xFFFF));
    put16(out, (uint16_t)((v >> 16) & 0xFFFF));
}

//时间戳转 DOS 时间（zip 用，精度 2 秒足够）
void dos_datetime(uint16_t& d, uint16_t& t) {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    tmv = *std::localtime(&now);
#endif
    d = (uint16_t)(((tmv.tm_year - 80) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday);
    t = (uint16_t)((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2));
}

bool write_zip(const std::string& path, const std::vector<ZipItem>& items) {
    std::string out;
    struct Central {
        std::string name;
        uint32_t crc = 0;
        uint32_t size = 0;
        uint32_t offset = 0;
    };
    std::vector<Central> central;
    uint16_t d = 0, t = 0;
    dos_datetime(d, t);

    for (const auto& it : items) {
        Central c;
        c.name = it.name;
        c.crc = crc32_of(it.data);
        c.size = (uint32_t)it.data.size();
        c.offset = (uint32_t)out.size();

        //本地文件头
        put32(out, 0x04034b50);
        put16(out, 20);      // version needed
        put16(out, 0x0800);  // UTF-8 文件名
        put16(out, 0);       // store
        put16(out, t);
        put16(out, d);
        put32(out, c.crc);
        put32(out, c.size);
        put32(out, c.size);
        put16(out, (uint16_t)c.name.size());
        put16(out, 0);
        out += c.name;
        out += it.data;
        central.push_back(c);
    }

    uint32_t central_offset = (uint32_t)out.size();
    for (const auto& c : central) {
        put32(out, 0x02014b50);
        put16(out, 20);
        put16(out, 20);
        put16(out, 0x0800);
        put16(out, 0);
        put16(out, t);
        put16(out, d);
        put32(out, c.crc);
        put32(out, c.size);
        put32(out, c.size);
        put16(out, (uint16_t)c.name.size());
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, 0);
        put32(out, c.offset);
        out += c.name;
    }
    uint32_t central_size = (uint32_t)out.size() - central_offset;

    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, (uint16_t)central.size());
    put16(out, (uint16_t)central.size());
    put32(out, central_size);
    put32(out, central_offset);
    put16(out, 0);

    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(out.data(), (std::streamsize)out.size());
    return f.good();
}

//脱敏：把 Cookie / token 的值替换成 ***
std::string redact(const std::string& text) {
    std::string s = text;
    static const char* keys[] = {"SESSDATA", "bili_jct", "DedeUserID__ckMd5", "DedeUserID",
                                 "access_key", "refresh_token", "buvid3", "buvid4", "ak",
                                 //设置里的常见敏感字段（小写，JSON 形态）
                                 "token", "cookie", "secret", "password", "sessdata"};
    for (const char* k : keys) {
        // key=value 形式（cookie 或查询串）
        std::regex re(std::string("(") + k + "=)([^;&\\s\"']+)");
        s = std::regex_replace(s, re, "$1***");
        // JSON 里的 "key": "value"
        std::regex re2(std::string("\"") + k + "\"\\s*:\\s*\"[^\"]*\"");
        s = std::regex_replace(s, re2, std::string("\"") + k + "\": \"***\"");
    }
    return s;
}

std::string tail_of_file(const std::string& path, size_t max_bytes) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return std::string();
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size <= 0) return std::string();
    std::streamoff start = size > (std::streamoff)max_bytes ? size - (std::streamoff)max_bytes : 0;
    f.seekg(start, std::ios::beg);
    std::string buf((size_t)(size - start), '\0');
    f.read(&buf[0], (std::streamsize)buf.size());
    buf.resize((size_t)f.gcount());
    return buf;
}

//收集系统信息（版本号、CPU 架构等），不涉及任何隐私路径
json system_info() {
    json info;
#ifdef _WIN32
    OSVERSIONINFOEXW os{};
    os.dwOSVersionInfoSize = sizeof(os);
    if (GetVersionExW((OSVERSIONINFOW*)&os)) {
        info["os"] = "Windows " + std::to_string(os.dwMajorVersion) + "." +
                     std::to_string(os.dwMinorVersion) + " build " + std::to_string(os.dwBuildNumber);
    }
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    info["arch"] = si.wProcessorArchitecture == 9 ? "x64" : "x86";
#else
    info["os"] = "unknown";
#endif
    info["time"] = now_string();
    info["cwd"] = fs::current_path().u8string();
    return info;
}

}  // namespace

//生成诊断 zip；args: {output_dir, note, contact, app_version, gui_log, settings, repo}
json api_feedback_bundle(const json& args) {
    std::string out_dir = args.value("output_dir", path_join(ctx().data_dir, "feedback"));
    ensure_dir(out_dir);

    std::string stamp = now_string();
    for (char& c : stamp)
        if (c == ':' || c == ' ' || c == '-') c = '_';
    std::string zip_path = path_join(out_dir, "bili_asset_box_feedback_" + stamp + ".zip");

    std::vector<ZipItem> items;

    // 1) 用户描述
    std::string note = args.value("note", "");
    if (note.empty()) note = "（用户没有填写问题描述）";
    items.push_back({"note.txt", "问题描述：\n" + note + "\n\n联系方式：" +
                                     args.value("contact", "（未填写）") + "\n"});

    // 2) 环境信息
    json info = system_info();
    info["app_version"] = args.value("app_version", "unknown");
    info["core_version"] = args.value("core_version", "unknown");
    info["python"] = args.value("python_version", "unknown");
    info["logged_in"] = ctx().logged_in;
    info["uname"] = ctx().logged_in ? ctx().uname : "";
    if (args.contains("settings") && args["settings"].is_object()) {
        json s = args["settings"];
        s.erase("cookies");
        s.erase("sessdata");
        info["settings"] = s;
    }
    items.push_back({"info.json", redact(info.dump(2))});

    // 3) 内核日志（只带最后 256KB）
    std::string core_log = path_join(ctx().data_dir, "logs/core.log");
    if (file_exists(core_log)) {
        std::string tail = tail_of_file(core_log, 256 * 1024);
        if (!tail.empty()) items.push_back({"logs/core.log", redact(tail)});
    }

    // 4) GUI 日志（可选）
    std::string gui_log = args.value("gui_log", "");
    if (!gui_log.empty() && file_exists(gui_log)) {
        std::string tail = tail_of_file(gui_log, 256 * 1024);
        if (!tail.empty()) items.push_back({"logs/gui.log", redact(tail)});
    }

    // 5) 依赖版本
    items.push_back({"versions.txt", "app=" + info["app_version"].get<std::string>() +
                                          "\ncore=" + info["core_version"].get<std::string>() +
                                          "\npython=" + info["python"].get<std::string>() + "\n"});

    if (items.size() > 40) items.resize(40);
    if (!write_zip(zip_path, items)) throw Error(-5, "写诊断包失败: " + zip_path);

    json result;
    result["zip"] = zip_path;
    result["size"] = (int64_t)file_size(zip_path);
    result["entries"] = (int)items.size();
    return result;
}

}  // namespace bc
