"""icon.py —— 应用图标加载（窗口、任务栏、托盘共用）。

优先用 assets/icon.ico（多尺寸，Windows 任务栏/Alt-Tab 都清楚），
没有就退回 assets/icon.png。全部用绝对路径，从任何工作目录启动都能找到。
"""
from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtGui import QIcon

# 打包成 exe 后资源在 _MEIPASS 下；源码运行时在项目根
if getattr(sys, "frozen", False):
    ROOT = Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
else:
    ROOT = Path(__file__).resolve().parents[1]

APP_USER_MODEL_ID = "BiliAssetBox.Desktop.1"

_cache: QIcon | None = None


def app_icon() -> QIcon:
    """返回应用图标（带缓存），文件都不存在时返回空 QIcon 也不报错。"""
    global _cache
    if _cache is not None:
        return _cache
    for name in ("icon.ico", "icon.png"):
        p = ROOT / "assets" / name
        if p.exists():
            icon = QIcon(str(p))
            if not icon.isNull():
                _cache = icon
                return icon
    _cache = QIcon()
    return _cache


def set_windows_app_id() -> None:
    """给进程设置显式 AppUserModelID。

    不设的话，从 pythonw.exe 启动的窗口会被任务栏归到 Python 那一组，
    图标也就跟着变成 Python 的标志；设了之后任务栏按我们自己的图标分组。
    非 Windows 或调用失败时静默跳过。
    """
    if sys.platform != "win32":
        return
    try:
        import ctypes

        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(APP_USER_MODEL_ID)
    except Exception:
        pass
