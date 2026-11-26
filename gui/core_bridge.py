"""core_bridge.py —— C++ 内核（bili_core.dll）的 ctypes 封装。

为什么用 ctypes 而不是 pybind11：
  * 不用为每个 Python 小版本重新编译绑定；
  * 跨语言边界只传 UTF-8 JSON 字符串，接口面小、好排查；
  * 内核内部把异常全部吃掉转成错误码，Python 侧只会看到 CoreError，
    不会出现"C++ 异常穿过 DLL 边界"这种未定义行为。

线程约定：
  * Core.call() 是同步阻塞的，只能在后台线程里调用（GUI 里用 ApiWorker）；
  * 下载任务通过 job_* 系列函数异步执行，主线程只负责 poll 事件。
"""
from __future__ import annotations

import ctypes
import json
import os
import threading
from pathlib import Path

from PySide6.QtCore import QObject, QTimer, Signal

#路径统一从 paths 拿：源码运行时是项目根，打包成 exe 后是 exe 所在目录
from .paths import APP_DIR, BUNDLE_DIR, DLL_PATH, SETTINGS_PATH

ROOT = APP_DIR


class CoreError(Exception):
    """内核返回的非 0 错误码。code 与 bili_core.h 里的约定一致。"""

    def __init__(self, code: int, message: str, op: str = ""):
        super().__init__(message)
        self.code = code
        self.message = message
        self.op = op

    def hint(self) -> str:
        """把错误码翻译成一句人话，直接显示在界面上。"""
        return {
            -1: "参数不对",
            -2: "网络请求失败（可能被风控或网络不通）",
            -3: "返回数据解析失败",
            -4: "需要登录，或登录已过期",
            -5: "写入文件失败（检查目录权限/磁盘空间）",
            -6: "文件校验失败",
            -7: "操作已取消",
            -8: "不支持的操作",
        }.get(self.code, "内核内部错误")


class Core:
    """对 DLL 导出函数的薄封装。一个进程只建一个实例（见文件末尾 core()）。"""

    def __init__(self, dll_path: Path | None = None):
        dll_path = Path(dll_path or DLL_PATH)
        if not dll_path.exists():
            raise FileNotFoundError(f"找不到内核 DLL：{dll_path}，请先运行 scripts/build_core.py")
        self._dll = ctypes.WinDLL(str(dll_path))
        self._lock = threading.Lock()  # bc_call 内部有自己的锁，这里只是防止重复 init
        self._init_done = False
        self._setup_signatures()

    #基础声明
    def _setup_signatures(self) -> None:
        cp = ctypes.c_char_p
        i32p = ctypes.POINTER(ctypes.c_int32)
        dll = self._dll

        dll.bc_init.argtypes = [cp, ctypes.POINTER(cp)]
        dll.bc_init.restype = ctypes.c_int

        dll.bc_version.argtypes = []
        dll.bc_version.restype = cp

        dll.bc_call.argtypes = [cp, cp, ctypes.POINTER(cp)]
        dll.bc_call.restype = ctypes.c_int

        dll.bc_job_create.argtypes = [cp, i32p, ctypes.POINTER(cp)]
        dll.bc_job_create.restype = ctypes.c_int

        dll.bc_job_start.argtypes = [ctypes.c_int32, ctypes.POINTER(cp)]
        dll.bc_job_start.restype = ctypes.c_int

        dll.bc_job_cancel.argtypes = [ctypes.c_int32, ctypes.POINTER(cp)]
        dll.bc_job_cancel.restype = ctypes.c_int

        dll.bc_job_poll.argtypes = [ctypes.c_int32, ctypes.c_int32, ctypes.POINTER(cp)]
        dll.bc_job_poll.restype = ctypes.c_int

        dll.bc_job_free.argtypes = [ctypes.c_int32, ctypes.POINTER(cp)]
        dll.bc_job_free.restype = ctypes.c_int

        dll.bc_free.argtypes = [cp]
        dll.bc_free.restype = None

    #内部工具
    def _take(self, ptr) -> str:
        """把 DLL 返回的 char* 转成 str，并立刻 free，避免泄漏。"""
        if not ptr:
            return ""
        try:
            raw = ctypes.cast(ptr, ctypes.c_char_p).value or b""
            return raw.decode("utf-8", errors="replace")
        finally:
            self._dll.bc_free(ptr)

    def _take_json(self, ptr) -> dict:
        text = self._take(ptr)
        if not text:
            return {}
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return {"_raw": text}

    #生命周期
    def init(self, config: dict | None = None) -> dict:
        """初始化内核：设置数据目录、CA、代理等。重复调用是幂等的。"""
        cfg = {
            "data_dir": str(ROOT / "data"),
            "cache_dir": str(ROOT / ".cache"),
            "ca_bundle": str(BUNDLE_DIR / "third_party" / "ca-bundle.crt"),
            "log_file": str(ROOT / "data" / "logs" / "core.log"),
        }
        if config:
            cfg.update({k: v for k, v in config.items() if v not in (None, "")})
        #目录不存在时内核会建，但先把父目录准备好更稳
        Path(cfg["data_dir"]).mkdir(parents=True, exist_ok=True)
        out = ctypes.c_char_p()
        rc = self._dll.bc_init(json.dumps(cfg, ensure_ascii=False).encode("utf-8"), ctypes.byref(out))
        info = self._take_json(out)
        if rc != 0:
            raise CoreError(rc, info.get("error", {}).get("message", "初始化失败"), "init")
        with self._lock:
            self._init_done = True
        return info

    @property
    def initialized(self) -> bool:
        return self._init_done

    def version(self) -> str:
        v = self._dll.bc_version()
        return v.decode("utf-8", errors="replace") if v else "?"

    #同步调用
    def call(self, op: str, args: dict | None = None) -> dict:
        """调用一个查询类接口，返回解析好的 dict；失败抛 CoreError。"""
        out = ctypes.c_char_p()
        payload = json.dumps(args or {}, ensure_ascii=False).encode("utf-8")
        rc = self._dll.bc_call(op.encode("utf-8"), payload, ctypes.byref(out))
        data = self._take_json(out)
        if rc != 0:
            err = data.get("error", {}) if isinstance(data, dict) else {}
            raise CoreError(rc, err.get("message", f"{op} 调用失败"), op)
        return data

    #任务接口
    def job_create(self, spec: dict) -> int:
        out = ctypes.c_char_p()
        jid = ctypes.c_int32(0)
        rc = self._dll.bc_job_create(
            json.dumps(spec, ensure_ascii=False).encode("utf-8"), ctypes.byref(jid), ctypes.byref(out)
        )
        info = self._take_json(out)
        if rc != 0:
            raise CoreError(rc, info.get("error", {}).get("message", "创建任务失败"), "job_create")
        return int(jid.value)

    def job_start(self, job_id: int) -> dict:
        out = ctypes.c_char_p()
        rc = self._dll.bc_job_start(ctypes.c_int32(job_id), ctypes.byref(out))
        data = self._take_json(out)
        if rc != 0:
            raise CoreError(rc, data.get("error", {}).get("message", "启动任务失败"), "job_start")
        return data

    def job_cancel(self, job_id: int) -> None:
        out = ctypes.c_char_p()
        self._dll.bc_job_cancel(ctypes.c_int32(job_id), ctypes.byref(out))
        self._take(out)  #结果用不上，但内存必须还

    def job_poll(self, job_id: int, timeout_ms: int = 0) -> dict:
        out = ctypes.c_char_p()
        rc = self._dll.bc_job_poll(ctypes.c_int32(job_id), ctypes.c_int32(timeout_ms), ctypes.byref(out))
        data = self._take_json(out)
        if rc != 0:
            raise CoreError(rc, data.get("error", {}).get("message", "读取任务失败"), "job_poll")
        return data

    def job_free(self, job_id: int) -> None:
        out = ctypes.c_char_p()
        self._dll.bc_job_free(ctypes.c_int32(job_id), ctypes.byref(out))
        self._take(out)


class JobTracker(QObject):
    """轮询一个或多个下载任务，把事件转成 Qt 信号。

    为什么不在 C++ 里回调 Python：回调需要 GIL、线程亲和性和重入保护，
    一旦内核持有内部锁再进 Python 很容易死锁。改成"Python 定时拉队列"
    最简单也最安全，120ms 的间隔对界面来说完全够用。
    """

    job_event = Signal(dict)     #单个文件事件（file_start/progress/file_done/...）
    job_finished = Signal(dict)  #整个任务结束
    job_progress = Signal(dict)  #汇总进度 {job_id,total,done,skipped,failed,bytes_done,state}

    def __init__(self, core: Core, parent: QObject | None = None):
        super().__init__(parent)
        self.core = core
        self.jobs: dict[int, dict] = {}
        self.timer = QTimer(self)
        self.timer.setInterval(120)
        self.timer.timeout.connect(self._tick)

    def start(self, spec: dict, meta: dict | None = None) -> int:
        jid = self.core.job_create(spec)
        self.core.job_start(jid)
        self.jobs[jid] = {
            "total": len(spec.get("items", [])) or 1,
            "done": 0,
            "skipped": 0,
            "failed": 0,
            "bytes_done": 0,
            "meta": meta or {},
            "title": (meta or {}).get("title") or spec.get("title", "下载任务"),
        }
        if not self.timer.isActive():
            self.timer.start()
        return jid

    def cancel(self, job_id: int) -> None:
        try:
            self.core.job_cancel(job_id)
        except CoreError:
            pass

    def stop_all(self) -> None:
        for jid in list(self.jobs):
            self.cancel(jid)

    def _tick(self) -> None:
        if not self.jobs:
            self.timer.stop()
            return
        for jid in list(self.jobs):
            try:
                r = self.core.job_poll(jid, 0)
            except CoreError as exc:
                self.job_finished.emit({"job_id": jid, "state": "failed", "message": exc.message})
                self.jobs.pop(jid, None)
                continue
            for ev in r.get("events", []):
                ev = dict(ev)
                ev["job_id"] = jid
                self.job_event.emit(ev)
            info = self.jobs[jid]
            info.update(
                {
                    "done": r.get("done", info["done"]),
                    "skipped": r.get("skipped", info["skipped"]),
                    "failed": r.get("failed", info["failed"]),
                    "bytes_done": r.get("bytes_done", info["bytes_done"]),
                    "state": r.get("state", "running"),
                }
            )
            self.job_progress.emit({"job_id": jid, **{k: info[k] for k in ("total", "done", "skipped", "failed", "bytes_done", "state")}})
            if r.get("finished"):
                payload = {"job_id": jid, "title": info["title"], **{k: info[k] for k in ("total", "done", "skipped", "failed", "bytes_done")}}
                payload["state"] = r.get("state", "done")
                try:
                    self.core.job_free(jid)
                except CoreError:
                    pass
                self.jobs.pop(jid, None)
                self.job_finished.emit(payload)
        if not self.jobs:
            self.timer.stop()


#单例
_core: Core | None = None


def core() -> Core:
    global _core
    if _core is None:
        _core = Core()
    return _core


def load_settings() -> dict:
    """读取 settings.json；没有就返回一份默认值。"""
    defaults = {
        "data_dir": str(ROOT / "data"),
        "download_dir": str(ROOT / "downloads"),
        "ffmpeg": "",
        "proxy": "",
        "concurrency": 6,
        "api_interval_ms": 500,
        "file_interval_ms": 150,
        "per_file_kbps": 0,
        "last_room": "",
        #留空表示用 feedback.py 里的默认仓库
        "feedback_repo": "",
    }
    if SETTINGS_PATH.exists():
        try:
            saved = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))
            if isinstance(saved, dict):
                defaults.update(saved)
        except (OSError, json.JSONDecodeError):
            pass
    return defaults


def save_settings(data: dict) -> None:
    SETTINGS_PATH.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def bootstrap_core() -> Core:
    """按设置初始化内核。GUI 启动时调用一次。"""
    st = load_settings()
    c = core()
    if not c.initialized:
        c.init(
            {
                "data_dir": st["data_dir"],
                "proxy": st.get("proxy", ""),
                "rate": {
                    "api_interval_ms": int(st.get("api_interval_ms", 500)),
                    "file_interval_ms": int(st.get("file_interval_ms", 150)),
                    "per_file_kbps": int(st.get("per_file_kbps", 0)),
                },
            }
        )
    return c
