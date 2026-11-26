// login.h —— 登录态持久化（DPAPI 加密的 session.bin）
#pragma once

namespace bc {

//把当前 HttpClient 里的 Cookie 加密后写入 <data_dir>/session.bin
void persist_session();

//从磁盘恢复 Cookie（bc_init 时调用；失败只记日志）
void load_session_from_disk();

}  // namespace bc
