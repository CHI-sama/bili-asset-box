"""thumbs.py —— 缩略图异步加载。

B 站的封面图都是 https 的 i0/i1.hdslb.com，直接 GET 就行（不需要 Cookie）。
为了不卡界面，这里用 QThreadPool + urllib 在后台线程下载，落盘缓存到
.cache/thumbs/，下次启动直接读本地。

注意：这里**不**走 C++ 内核。缩略图数量多、体积小、失败也无所谓，
用 Python 自己下载更省事，也不会占用内核的下载任务和限速额度。
"""
from __future__ import annotations

import hashlib
import urllib.request
from pathlib import Path

from PySide6.QtCore import QObject, QRect, QRunnable, Qt, QThreadPool, Signal
from PySide6.QtGui import QColor, QFont, QImage, QPainter

from ..core_bridge import ROOT

CACHE_DIR = ROOT / ".cache" / "thumbs"
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"

# Qt 能直接解码的图片扩展名；其余（json/bin/zip/mp4…）用占位图代替
IMAGE_EXT = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".svg", ".avif", ".ico"}


def ext_of_url(url: str) -> str:
    """取 URL 的小写扩展名（带点），没有就返回空串。"""
    path = url.split("?")[0].split("#")[0]
    dot = path.rfind(".")
    return path[dot:].lower() if dot != -1 and len(path) - dot <= 6 else ""


_placeholder_cache: dict[str, QImage] = {}


def placeholder_image(text: str, size: int = 150) -> QImage:
    """给非图片素材画一张占位图：深色底 + 居中类型文字（不联网）。"""
    key = f"{text}:{size}"
    if key in _placeholder_cache:
        return _placeholder_cache[key]
    img = QImage(size, size, QImage.Format_ARGB32)
    img.fill(QColor("#1B1F29"))
    p = QPainter(img)
    p.setRenderHint(QPainter.Antialiasing)
    p.setPen(QColor("#39404E"))
    p.drawRoundedRect(QRect(6, 6, size - 12, size - 12), 10, 10)
    p.setPen(QColor("#8A94A6"))
    f = QFont("Microsoft YaHei UI")
    f.setPixelSize(max(14, size // 7))
    f.setBold(True)
    p.setFont(f)
    p.drawText(QRect(0, 0, size, size), Qt.AlignCenter, text)
    p.end()
    _placeholder_cache[key] = img
    return img


class _Signals(QObject):
    done = Signal(str, QImage)  # url, 图片


class _Fetch(QRunnable):
    def __init__(self, url: str, signals: _Signals, cache: Path):
        super().__init__()
        self.url = url
        self.signals = signals
        self.cache = cache
        self.setAutoDelete(True)

    def run(self) -> None:
        data = b""
        #先看磁盘缓存
        if self.cache.exists():
            try:
                data = self.cache.read_bytes()
            except OSError:
                data = b""
        if not data:
            try:
                req = urllib.request.Request(
                    self.url, headers={"User-Agent": UA, "Referer": "https://www.bilibili.com/"}
                )
                with urllib.request.urlopen(req, timeout=15) as resp:
                    data = resp.read()
                if data:
                    CACHE_DIR.mkdir(parents=True, exist_ok=True)
                    self.cache.write_bytes(data)
            except Exception:
                data = b""
        if not data:
            return
        img = QImage()
        if img.loadFromData(data):
            self.signals.done.emit(self.url, img)


class ThumbPool(QObject):
    """全应用共享的缩略图加载器：按 URL 去重、内存缓存。"""

    image_ready = Signal(str, QImage)

    def __init__(self, parent: QObject | None = None):
        super().__init__(parent)
        self._pool = QThreadPool(self)
        self._pool.setMaxThreadCount(6)
        self._signals = _Signals()
        self._signals.done.connect(self._on_done)
        self._memory: dict[str, QImage] = {}
        self._pending: set[str] = set()

    def get(self, url: str) -> QImage | None:
        """有缓存立刻返回，没有就返回 None 并触发后台加载。"""
        if not url:
            return None
        if url in self._memory:
            return self._memory[url]
        if url not in self._pending:
            self._pending.add(url)
            key = hashlib.md5(url.encode("utf-8")).hexdigest()
            self._pool.start(_Fetch(url, self._signals, CACHE_DIR / f"{key}.img"))
        return None

    def _on_done(self, url: str, img: QImage) -> None:
        self._pending.discard(url)
        self._memory[url] = img
        self.image_ready.emit(url, img)

    def shutdown(self) -> None:
        self._pool.clear()
