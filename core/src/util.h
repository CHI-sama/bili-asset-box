// util.h —— 公共工具：编码转换、哈希、Base64、路径/文件名、日志
//全部为项目内部使用，不导出到 DLL 外。
#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace bc {

//内部异常：只在 DLL 内部抛出，ABI 层统一 catch 后转成错误码。
struct Error : std::runtime_error {
    int code;
    Error(int c, const std::string& msg) : std::runtime_error(msg), code(c) {}
};

// UTF-8 <-> UTF-16（Windows 下所有宽字符 API 都需要）
std::wstring utf8_to_wide(const std::string& s);
std::string wide_to_utf8(const std::wstring& s);

//路径与文件
bool file_exists(const std::string& path);
bool dir_exists(const std::string& path);
void ensure_dir(const std::string& path);
std::string path_join(const std::string& a, const std::string& b);
std::string file_ext(const std::string& path);          //含点，小写，例如 ".png"
std::string read_text_file(const std::string& path);    //失败抛 Error(-5)
void write_text_file(const std::string& path, const std::string& data);
void remove_file_quiet(const std::string& path);
uint64_t file_size(const std::string& path);            //不存在返回 0
//读文件开头若干字节（用来嗅探"下载到网页"的风控页），失败返回空串
std::string read_text_file_head(const std::string& path, size_t bytes = 16);

//哈希
std::string md5_hex(const std::string& data);
std::string sha256_hex(const std::string& data);
std::string sha256_file_hex(const std::string& path);

// Base64（DPAPI 密文用；不处理换行）
std::string b64_encode(const std::string& data);
std::string b64_decode(const std::string& text);

// URL 辅助
// bilibili 的图片 CDN 支持 @ 后缀做缩放/裁切，例如 x.png@48w_48h.webp。
//想要"原图"就把它砍掉。
std::string strip_hdslb_suffix(const std::string& url);
//把 http 换成 https，并保证协议头存在
std::string normalize_url(const std::string& url);
//百分号编码（等价 JS encodeURIComponent）
std::string url_encode(const std::string& s);

//文件名清洗：非法字符、Windows 保留名、超长截断
std::string sanitize_filename(const std::string& name, int max_len = 120);
//由 URL 猜一个文件名（去掉查询串）
std::string filename_from_url(const std::string& url);
//去掉字符串首尾空白
std::string trim(const std::string& s);

//时间
int64_t now_ms();
std::string now_string();  // "2026-11-26 11:30:00"

//日志：写到 config 里的 log_file（未设置则丢弃），线程安全
void log_init(const std::string& file_path);
void log_msg(const std::string& msg);

}  // namespace bc
