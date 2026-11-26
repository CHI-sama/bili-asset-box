/*
 * bili_core.h —— BiliAssetBox 的 C ABI（纯 C，导出给 Python ctypes 使用）
 *
 * 设计要点
 *  - 全部导出名都是 C 链接（extern "C"），不会被 C++ 名字修饰污染。
 *  - 字符串一律 UTF-8，输出用 char** 返回，调用方用完必须 bc_free()。
 *  - 任何异常都在 DLL 内部被吃掉，转换成错误码 + bc_last_error()，绝不允许
 *    跨语言边界抛出（跨 DLL 抛异常在 Windows 上是未定义行为）。
 *  - 进度通过 bc_job_poll() 事件队列拉取，不走回调：Python 的 GIL、线程亲和性
 *    和重入问题都绕开了，GUI 只要定时 poll 即可。
 *
 * 错误码约定
 *   0    成功
 *   -1   参数错误
 *   -2   网络错误
 *   -3   解析错误
 *   -4   未登录 / 登录失效
 *   -5   磁盘错误
 *   -6   校验失败
 *   -7   已取消
 *   -8   不支持的操作
 *   -99  内部错误
 */
#ifndef BILI_CORE_H
#define BILI_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  ifdef BILI_CORE_EXPORTS
#    define BC_API __declspec(dllexport)
#  else
#    define BC_API __declspec(dllimport)
#  endif
#  define BC_CALL __cdecl
#else
#  define BC_API __attribute__((visibility("default")))
#  define BC_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化全局状态。config_json 可传 "{}"。
 * 支持字段：{"data_dir": "...", "cache_dir": "...", "proxy": "http://...",
 *            "ca_bundle": "...", "log_file": "...", "user_agent": "..."}
 * 成功返回 0；重复调用是幂等的。 */
BC_API int BC_CALL bc_init(const char* config_json_utf8, char** out_json);

/* 返回静态字符串（无需释放），例如 "0.1.0"。 */
BC_API const char* BC_CALL bc_version(void);

/* 通用同步调用：op 是操作名，args_json 是参数对象，out_json 返回结果对象。
 * 这是所有"查询类"接口的统一入口，例如：
 *   bc_call("suit_detail", "{\"item_id\":5766}", &out);
 * 返回 0 时 out_json 一定非空（至少是 "{}"）；非 0 时 out_json 里是
 * {"error":{"code":-4,"message":"...","op":"..."}}。 */
BC_API int BC_CALL bc_call(const char* op, const char* args_json_utf8, char** out_json);

/* 创建一个下载任务（仅是描述，不启动）。spec 见 docs/ABI.md。
 * out_job_id 返回任务句柄（从 1 开始递增，0 表示失败）。 */
BC_API int BC_CALL bc_job_create(const char* spec_json_utf8, int32_t* out_job_id, char** out_json);

/* 启动任务：内部丢到线程池，立即返回。 */
BC_API int BC_CALL bc_job_start(int32_t job_id, char** out_json);

/* 请求取消：置取消标志，下载循环在下一个数据块处退出。 */
BC_API int BC_CALL bc_job_cancel(int32_t job_id, char** out_json);

/* 拉取事件（阻塞至多 timeout_ms 毫秒，timeout_ms=0 表示立即返回）。
 * 返回 {"events":[...], "finished":bool, "state":"..."}。 */
BC_API int BC_CALL bc_job_poll(int32_t job_id, int32_t timeout_ms, char** out_json);

/* 释放任务及其事件缓冲。任务必须已结束（或先 cancel）。 */
BC_API int BC_CALL bc_job_free(int32_t job_id, char** out_json);

/* 取当前线程最后一次错误的 JSON；没有错误时返回 "{}"。 */
BC_API int BC_CALL bc_last_error(char** out_json);

/* 释放 DLL 分配的内存。传 NULL 是安全的。 */
BC_API void BC_CALL bc_free(char* p);

/* 计算文件的 SHA-256，返回 64 位小写十六进制。 */
BC_API int BC_CALL bc_sha256_file(const char* path_utf8, char** out_hex);

/* Windows DPAPI 加解密（用于本地保存 Cookie，密文只对当前用户可解）。
 * 输出为 base64 文本。非 Windows 平台返回 -8。 */
BC_API int BC_CALL bc_dpapi_protect(const char* data_utf8, char** out_b64);
BC_API int BC_CALL bc_dpapi_unprotect(const char* b64, char** out_utf8);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BILI_CORE_H */
