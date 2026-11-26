"""app.py —— GUI 入口。

启动顺序：
  1. 准备日志（logs/gui.log），把未捕获异常也写进去，方便用户反馈时附上；
  2. 创建 QApplication、套上深色主题；
  3. 初始化内核（bili_core.dll），失败就弹窗说明怎么编译；
  4. 建主窗口并进入事件循环。

命令行参数：
  --dll PATH   指定内核 DLL（默认 build/bin/bili_core.dll）
  --debug      打印更多日志到控制台
"""
from __future__ import annotations

import logging
import os
import sys
import traceback
from logging.handlers import RotatingFileHandler
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication, QMessageBox

from .paths import APP_DIR

ROOT = APP_DIR
LOG_DIR = ROOT / "logs"

log = logging.getLogger("bili")


def setup_logging(debug: bool = False) -> None:
    """日志同时写文件和控制台；文件按 2MB × 3 个滚动，反馈包只取末尾。"""
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    fmt = logging.Formatter("%(asctime)s %(levelname)-7s %(name)s: %(message)s")
    fh = RotatingFileHandler(LOG_DIR / "gui.log", maxBytes=2 * 1024 * 1024, backupCount=3, encoding="utf-8")
    fh.setFormatter(fmt)
    fh.setLevel(logging.DEBUG if debug else logging.INFO)
    log.addHandler(fh)
    if debug:
        sh = logging.StreamHandler()
        sh.setFormatter(fmt)
        log.addHandler(sh)
    log.setLevel(logging.DEBUG if debug else logging.INFO)
    log.propagate = False


def install_excepthook() -> None:
    """未捕获异常：写日志 + 弹窗，不让窗口"闪一下就没"。"""

    def hook(exc_type, exc, tb):
        text = "".join(traceback.format_exception(exc_type, exc, tb))
        log.error("未捕获异常:\n%s", text)
        #离屏/无界面环境下弹窗会永久阻塞，直接打日志退出
        if os.environ.get("QT_QPA_PLATFORM", "").startswith("offscreen"):
            print(text, file=sys.stderr)
            return
        try:
            QMessageBox.critical(
                None,
                "出错了",
                f"{exc_type.__name__}: {exc}\n\n详细信息已写入 logs/gui.log，"
                "可以在「反馈」页生成诊断包。",
            )
        except Exception:
            print(text, file=sys.stderr)

    sys.excepthook = hook


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv if argv is None else argv)
    debug = "--debug" in argv
    dll_override = ""
    if "--dll" in argv:
        idx = argv.index("--dll")
        if idx + 1 < len(argv):
            dll_override = argv[idx + 1]

    setup_logging(debug)
    install_excepthook()
    log.info("启动 BiliAssetBox，Python %s", sys.version.split()[0])

    app = QApplication(argv)
    app.setApplicationName("BiliAssetBox")
    app.setApplicationDisplayName("B站素材盒子")
    app.setStyle("Fusion")
    app.setAttribute(Qt.AA_DontShowIconsInMenus, False)

    #图标：任务栏/Alt-Tab 用 ico；同时设置 AppUserModelID，
    #否则从 pythonw.exe 启动时任务栏会归到 Python 那组（显示 Python 图标）。
    from .icon import app_icon, set_windows_app_id

    set_windows_app_id()
    app.setWindowIcon(app_icon())

    from .theme import qss

    app.setStyleSheet(qss())

    # ---- 内核
    from .core_bridge import Core, CoreError, bootstrap_core, load_settings

    settings = load_settings()
    try:
        if dll_override:
            core = Core(Path(dll_override))
            st = load_settings()
            core.init(
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
        else:
            core = bootstrap_core()
    except FileNotFoundError as exc:
        log.error("%s", exc)
        QMessageBox.critical(None, "找不到内核", str(exc))
        return 2
    except CoreError as exc:
        log.error("内核初始化失败: %s", exc)
        QMessageBox.critical(None, "内核初始化失败", f"{exc.message}\n\n错误码 {exc.code}")
        return 3

    log.info("内核版本 %s，数据目录 %s", core.version(), settings.get("data_dir"))

    # ---- 主窗口
    from .main_window import MainWindow

    win = MainWindow(core, settings)
    win.show()
    rc = app.exec()
    #退出前给后台任务一点时间收尾，避免解释器拆解时线程还在碰对象
    from PySide6.QtCore import QThreadPool

    QThreadPool.globalInstance().waitForDone(3000)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
