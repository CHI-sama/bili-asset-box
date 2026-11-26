"""common.py —— 通用小组件 + 后台调用工具。

所有网络请求都必须离开主线程（bc_call 是阻塞的），这里用 QRunnable
把调用丢到全局线程池，结果通过信号回到主线程，页面只管接信号。
"""
from __future__ import annotations

import traceback

from PySide6.QtCore import QObject, QRunnable, Qt, QThreadPool, Signal
from PySide6.QtGui import QFontMetrics
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from ..core_bridge import Core, CoreError


class _Signals(QObject):
    ok = Signal(object)
    err = Signal(int, str)


#正在跑的后台任务必须被引用住：QRunnable 只被线程池持有，
#一旦信号对象被 GC，emit 会抛 "Signal source has been deleted"。
_LIVE_TASKS: set["_ApiTask"] = set()


class _ApiTask(QRunnable):
    """在后台线程里执行一次 core.call()。"""

    def __init__(self, core: Core, op: str, args: dict):
        super().__init__()
        self.core = core
        self.op = op
        self.args = args
        self.signals = _Signals()
        self.setAutoDelete(True)

    def run(self) -> None:
        try:
            result = self.core.call(self.op, self.args)
            try:
                self.signals.ok.emit(result)
            except RuntimeError:
                pass  #窗口已经销毁，结果没人要了，直接丢掉
        except CoreError as exc:
            try:
                self.signals.err.emit(exc.code, exc.message)
            except RuntimeError:
                pass
        except Exception as exc:  #兜底：绝不让后台线程静默死掉
            traceback.print_exc()
            try:
                self.signals.err.emit(-99, str(exc))
            except RuntimeError:
                pass
        finally:
            _LIVE_TASKS.discard(self)


def api_async(core: Core, op: str, args: dict | None, on_ok, on_err=None) -> None:
    """异步调用内核接口；on_ok(dict) / on_err(code, message) 在主线程被调用。"""
    task = _ApiTask(core, op, args or {})
    task.signals.ok.connect(on_ok)
    if on_err is not None:
        task.signals.err.connect(on_err)
    else:
        task.signals.err.connect(lambda code, msg: print(f"[api] {op} 失败({code}): {msg}"))
    _LIVE_TASKS.add(task)
    QThreadPool.globalInstance().start(task)


#UI 小件
def page_header(title: str, desc: str = "") -> QWidget:
    """页面顶部的大标题 + 说明文字。"""
    box = QWidget()
    lay = QVBoxLayout(box)
    lay.setContentsMargins(0, 0, 0, 6)
    lay.setSpacing(3)
    t = QLabel(title)
    t.setObjectName("PageTitle")
    lay.addWidget(t)
    if desc:
        d = QLabel(desc)
        d.setObjectName("PageDesc")
        d.setWordWrap(True)
        lay.addWidget(d)
    return box


def hline() -> QFrame:
    line = QFrame()
    line.setObjectName("HLine")
    line.setFixedHeight(1)
    line.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
    return line


def card(*, padding: int = 14) -> tuple[QFrame, QVBoxLayout]:
    """带边框的卡片容器，返回 (frame, 竖向布局)。"""
    frame = QFrame()
    frame.setObjectName("Card")
    lay = QVBoxLayout(frame)
    lay.setContentsMargins(padding, padding, padding, padding)
    lay.setSpacing(10)
    return frame, lay


class InfoBar(QFrame):
    """一行状态提示（成功绿/失败红/普通灰），3 秒后自动淡出。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Card")
        lay = QHBoxLayout(self)
        lay.setContentsMargins(12, 8, 12, 8)
        self.label = QLabel("")
        self.label.setWordWrap(True)
        lay.addWidget(self.label, 1)
        self.hide()
        self._timer = None

    def show_message(self, text: str, kind: str = "info", timeout_ms: int = 5000) -> None:
        color = {"info": "#9AA3B2", "ok": "#3DDC97", "warn": "#FFC757", "error": "#FF6B6B"}.get(kind, "#9AA3B2")
        self.label.setStyleSheet(f"color: {color};")
        self.label.setText(text)
        self.show()
        from PySide6.QtCore import QTimer

        if self._timer:
            self._timer.stop()
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self.hide)
        self._timer.start(timeout_ms)


def elide(label: QLabel, text: str, width: int) -> None:
    """按像素宽度省略长文本（卡片标题用）。"""
    fm = QFontMetrics(label.font())
    label.setText(fm.elidedText(text, Qt.ElideRight, width))


def primary_button(text: str) -> QPushButton:
    btn = QPushButton(text)
    btn.setObjectName("Primary")
    btn.setCursor(Qt.PointingHandCursor)
    return btn
