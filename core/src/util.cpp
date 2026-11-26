// util.cpp —— 工具函数实现（哈希为自带实现，避免额外链接依赖）
#include "util.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#  include <windows.h>
#endif

#include <filesystem>

namespace fs = std::filesystem;

namespace bc {

//编码转换
#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
#else
std::wstring utf8_to_wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
std::string wide_to_utf8(const std::wstring& w) { return std::string(w.begin(), w.end()); }
#endif

//文件
bool file_exists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(path), ec);
}

bool dir_exists(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(fs::u8path(path), ec);
}

void ensure_dir(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::u8path(path), ec);
    if (ec && !dir_exists(path)) {
        throw Error(-5, "无法创建目录: " + path + " (" + ec.message() + ")");
    }
}

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

std::string file_ext(const std::string& path) {
    std::string name = filename_from_url(path);
    auto pos = name.find_last_of('.');
    if (pos == std::string::npos) return std::string();
    std::string ext = name.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
}

std::string read_text_file(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) throw Error(-5, "无法读取文件: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_text_file(const std::string& path, const std::string& data) {
    fs::path p = fs::u8path(path);
    if (p.has_parent_path()) ensure_dir(wide_to_utf8(p.parent_path().wstring()));
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) throw Error(-5, "无法写入文件: " + path);
    f.write(data.data(), (std::streamsize)data.size());
    f.flush();
    if (!f) throw Error(-5, "写入文件失败: " + path);
}

void remove_file_quiet(const std::string& path) {
    std::error_code ec;
    fs::remove(fs::u8path(path), ec);
}

uint64_t file_size(const std::string& path) {
    std::error_code ec;
    auto sz = fs::file_size(fs::u8path(path), ec);
    return ec ? 0 : (uint64_t)sz;
}

//MD5
namespace {
struct MD5Ctx {
    uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
    uint64_t total = 0;
    uint8_t buf[64];
    size_t used = 0;
};

const uint32_t MD5_K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

const int MD5_S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                       5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                       4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                       6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

inline uint32_t rotl32(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

void md5_block(MD5Ctx& ctx, const uint8_t* p) {
    uint32_t M[16];
    for (int i = 0; i < 16; ++i) {
        M[i] = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) |
               ((uint32_t)p[i * 4 + 2] << 16) | ((uint32_t)p[i * 4 + 3] << 24);
    }
    uint32_t A = ctx.a, B = ctx.b, C = ctx.c, D = ctx.d;
    for (int i = 0; i < 64; ++i) {
        uint32_t F;
        int g;
        if (i < 16) {
            F = (B & C) | (~B & D);
            g = i;
        } else if (i < 32) {
            F = (D & B) | (~D & C);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            F = B ^ C ^ D;
            g = (3 * i + 5) % 16;
        } else {
            F = C ^ (B | ~D);
            g = (7 * i) % 16;
        }
        uint32_t tmp = D;
        D = C;
        C = B;
        B = B + rotl32(A + F + MD5_K[i] + M[g], MD5_S[i]);
        A = tmp;
    }
    ctx.a += A;
    ctx.b += B;
    ctx.c += C;
    ctx.d += D;
}

void md5_update(MD5Ctx& ctx, const uint8_t* data, size_t len) {
    ctx.total += len;
    while (len > 0) {
        size_t take = std::min(len, sizeof(ctx.buf) - ctx.used);
        memcpy(ctx.buf + ctx.used, data, take);
        ctx.used += take;
        data += take;
        len -= take;
        if (ctx.used == 64) {
            md5_block(ctx, ctx.buf);
            ctx.used = 0;
        }
    }
}

std::string md5_final(MD5Ctx& ctx) {
    uint64_t bits = ctx.total * 8;
    uint8_t pad = 0x80;
    md5_update(ctx, &pad, 1);
    uint8_t zero = 0;
    while (ctx.used != 56) md5_update(ctx, &zero, 1);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; ++i) lenbuf[i] = (uint8_t)(bits >> (8 * i));
    md5_update(ctx, lenbuf, 8);
    uint8_t out[16];
    uint32_t vals[4] = {ctx.a, ctx.b, ctx.c, ctx.d};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = (uint8_t)(vals[i] >> (8 * j));
    static const char* hexd = "0123456789abcdef";
    std::string s;
    s.reserve(32);
    for (int i = 0; i < 16; ++i) {
        s.push_back(hexd[out[i] >> 4]);
        s.push_back(hexd[out[i] & 0xf]);
    }
    return s;
}
}  // namespace

std::string md5_hex(const std::string& data) {
    MD5Ctx ctx;
    md5_update(ctx, (const uint8_t*)data.data(), data.size());
    return md5_final(ctx);
}

//SHA-256
namespace {
struct Sha256Ctx {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint64_t total = 0;
    uint8_t buf[64];
    size_t used = 0;
};

const uint32_t SHA_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr32(uint32_t x, int c) { return (x >> c) | (x << (32 - c)); }

void sha_block(Sha256Ctx& ctx, const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx.h[0], b = ctx.h[1], c = ctx.h[2], d = ctx.h[3];
    uint32_t e = ctx.h[4], f = ctx.h[5], g = ctx.h[6], h = ctx.h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + SHA_K[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx.h[0] += a; ctx.h[1] += b; ctx.h[2] += c; ctx.h[3] += d;
    ctx.h[4] += e; ctx.h[5] += f; ctx.h[6] += g; ctx.h[7] += h;
}

void sha_update(Sha256Ctx& ctx, const uint8_t* data, size_t len) {
    ctx.total += len;
    while (len > 0) {
        size_t take = std::min(len, sizeof(ctx.buf) - ctx.used);
        memcpy(ctx.buf + ctx.used, data, take);
        ctx.used += take;
        data += take;
        len -= take;
        if (ctx.used == 64) {
            sha_block(ctx, ctx.buf);
            ctx.used = 0;
        }
    }
}

std::string sha_final(Sha256Ctx& ctx) {
    uint64_t bits = ctx.total * 8;
    uint8_t pad = 0x80;
    sha_update(ctx, &pad, 1);
    uint8_t zero = 0;
    while (ctx.used != 56) sha_update(ctx, &zero, 1);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; ++i) lenbuf[i] = (uint8_t)(bits >> (8 * (7 - i)));
    sha_update(ctx, lenbuf, 8);
    static const char* hexd = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (int i = 0; i < 8; ++i) {
        for (int j = 3; j >= 0; --j) {
            uint8_t byte = (uint8_t)(ctx.h[i] >> (8 * j));
            s.push_back(hexd[byte >> 4]);
            s.push_back(hexd[byte & 0xf]);
        }
    }
    return s;
}
}  // namespace

std::string sha256_hex(const std::string& data) {
    Sha256Ctx ctx;
    sha_update(ctx, (const uint8_t*)data.data(), data.size());
    return sha_final(ctx);
}

std::string sha256_file_hex(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) throw Error(-5, "无法打开文件计算 SHA-256: " + path);
    Sha256Ctx ctx;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize got = f.gcount();
        if (got <= 0) break;
        sha_update(ctx, (const uint8_t*)buf.data(), (size_t)got);
    }
    return sha_final(ctx);
}

//Base64
std::string b64_encode(const std::string& data) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t v = ((uint8_t)data[i] << 16) | ((uint8_t)data[i + 1] << 8) | (uint8_t)data[i + 2];
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);
        out.push_back(tbl[v & 63]);
        i += 3;
    }
    size_t rest = data.size() - i;
    if (rest == 1) {
        uint32_t v = (uint8_t)data[i] << 16;
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out += "==";
    } else if (rest == 2) {
        uint32_t v = ((uint8_t)data[i] << 16) | ((uint8_t)data[i + 1] << 8);
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::string b64_decode(const std::string& text) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    uint32_t buf = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = val(c);
        if (v < 0) throw Error(-3, "Base64 解码失败：非法字符");
        buf = (buf << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((buf >> bits) & 0xff));
        }
    }
    return out;
}

//URL
std::string strip_hdslb_suffix(const std::string& url) {
    //只处理 hdslb / bilibili 图床；@ 后面是缩放参数
    auto at = url.find('@');
    if (at == std::string::npos) return url;
    std::string base = url.substr(0, at);
    //有些地址用 "?x-oss-process=" 也行，但 b 站主要是 @
    return base;
}

std::string normalize_url(const std::string& url) {
    std::string u = trim(url);
    if (u.empty()) return u;
    if (u.rfind("//", 0) == 0) return "https:" + u;
    if (u.rfind("http://", 0) == 0) return "https://" + u.substr(7);
    if (u.rfind("https://", 0) == 0) return u;
    return "https://" + u;
}

std::string url_encode(const std::string& s) {
    static const char* hexd = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' ||
            c == '\'' || c == '(' || c == ')') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hexd[c >> 4]);
            out.push_back(hexd[c & 0xf]);
        }
    }
    return out;
}

//文件名
std::string sanitize_filename(const std::string& name, int max_len) {
    std::string s = name;
    // Windows 非法字符
    for (char& c : s) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
            c == '|' || c == '?' || c == '*') {
            c = '_';
        }
        if ((unsigned char)c < 0x20) c = '_';
    }
    //去掉首尾空格和点（Windows 不允许以点或空格结尾）
    while (!s.empty() && (s.front() == ' ' || s.front() == '.')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    if (s.empty()) s = "unnamed";
    //保留设备名
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
                                     "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",
                                     "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    std::string stem = s;
    auto dot = stem.find('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    std::string upper = stem;
    for (char& c : upper) c = (char)std::toupper((unsigned char)c);
    for (const char* r : reserved) {
        if (upper == r) {
            s = "_" + s;
            break;
        }
    }
    //截断：注意 UTF-8 不能切半个字符
    if (max_len > 0 && (int)s.size() > max_len) {
        size_t cut = (size_t)max_len;
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;
        s = s.substr(0, cut);
    }
    return s;
}

std::string filename_from_url(const std::string& url) {
    std::string u = url;
    auto q = u.find_first_of("?#");
    if (q != std::string::npos) u = u.substr(0, q);
    auto slash = u.find_last_of('/');
    std::string name = (slash == std::string::npos) ? u : u.substr(slash + 1);
    // URL 里的中文可能是 %XX 编码，简单解码（仅处理 %XX，不处理 '+'）
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '%' && i + 2 < name.size()) {
            auto hexv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hexv(name[i + 1]), l = hexv(name[i + 2]);
            if (h >= 0 && l >= 0) {
                out.push_back((char)((h << 4) | l));
                i += 2;
                continue;
            }
        }
        out.push_back(name[i]);
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

//时间
int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string now_string() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

//日志
namespace {
std::mutex g_log_mutex;
std::string g_log_path;

void log_write_line(const std::string& line) {
    if (g_log_path.empty()) return;
    std::ofstream f(fs::u8path(g_log_path), std::ios::binary | std::ios::app);
    if (!f) return;
    f << line << "\n";
}
}  // namespace

void log_init(const std::string& file_path) {
    std::lock_guard<std::mutex> lk(g_log_mutex);
    g_log_path = file_path;
}

void log_msg(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_log_mutex);
    log_write_line("[" + now_string() + "] " + msg);
}

//文件头嗅探
//只读前 N 字节，避免为了判断类型把大文件整个读进内存。
std::string read_text_file_head(const std::string& path, size_t bytes) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return std::string();
    std::string buf(bytes, '\0');
    f.read(&buf[0], (std::streamsize)bytes);
    buf.resize((size_t)f.gcount());
    return buf;
}

}  // namespace bc
