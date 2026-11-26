"""pages/base.py —— 页面基类 + 下载拼接的公共逻辑。

所有页面都拿到同一个 MainWindow，通过它能访问 core / tracker / thumbs /
settings，以及跳转到详情页的方法（open_suit / open_collection）。
"""
from __future__ import annotations

import re

from pathlib import Path

from PySide6.QtWidgets import QVBoxLayout, QWidget

# Windows 文件名禁用字符 + 保留名
_ILLEGAL = re.compile(r'[\\/:*?"<>|\r\n\t]')
_RESERVED = {
    "CON", "PRN", "AUX", "NUL",
    *(f"COM{i}" for i in range(1, 10)),
    *(f"LPT{i}" for i in range(1, 10)),
}


def safe_name(name: str, limit: int = 60) -> str:
    """把标题/文件名清洗成 Windows 能接受的样子。"""
    name = _ILLEGAL.sub("_", (name or "未命名").strip())
    name = name.strip(" .") or "未命名"
    if name.upper() in _RESERVED:
        name += "_"
    if len(name) > limit:
        name = name[:limit].rstrip(" .")
    return name or "未命名"


def ext_of(url: str, default: str = ".png") -> str:
    """从 URL 猜扩展名（B 站图片一般没有查询串干扰）。"""
    path = url.split("?")[0].split("#")[0]
    dot = path.rfind(".")
    if dot != -1 and len(path) - dot <= 6:
        return path[dot:].lower()
    return default


class Page(QWidget):
    """带标题的标准页面。子类在 __init__ 里往 self.body 里塞控件。"""

    title = ""
    desc = ""

    def __init__(self, win, parent=None):
        super().__init__(parent)
        self.win = win
        self.core = win.core
        self.tracker = win.tracker
        self.thumbs = win.thumbs

        outer = QVBoxLayout(self)
        outer.setContentsMargins(26, 10, 26, 16)
        outer.setSpacing(12)
        #标题和说明由主窗口顶栏统一显示（Page.title / Page.desc），
        #页面内部不再重复画一遍标题。
        self.body = QVBoxLayout()
        self.body.setSpacing(12)
        outer.addLayout(self.body, 1)

    #会话信息
    @property
    def settings(self) -> dict:
        return self.win.settings

    def download_dir(self) -> Path:
        return Path(self.win.settings.get("download_dir") or (Path(self.win.root) / "downloads"))

    def notify(self, text: str, kind: str = "info") -> None:
        self.win.notify(text, kind)

    #启动下载
    def start_files(self, files: list[dict], title: str, out_dir: Path | None = None) -> int | None:
        """把 {url,name} 列表变成内核下载任务。

        name 里可以带子目录（如 suit_123/card_xxx.png），这样不同素材
        会自动落到不同文件夹。
        """
        out_dir = Path(out_dir or self.download_dir())
        items = []
        for f in files:
            url = f.get("url", "")
            if not url:
                continue
            name = f.get("name") or safe_name(url.split("/")[-1])
            # name 里的 "/" 视为子目录分隔
            parts = [safe_name(p, 80) for p in name.replace("\\", "/").split("/") if p]
            path = out_dir.joinpath(*parts)
            item = {"url": url, "path": str(path), "name": parts[-1]}
            if f.get("sha256"):
                item["sha256"] = f["sha256"]
            items.append(item)
        if not items:
            self.notify("没有可下载的内容", "warn")
            return None
        spec = {
            "type": "files",
            "out_dir": str(out_dir),
            "items": items,
            "concurrency": int(self.settings.get("concurrency", 6)),
            "title": title,
        }
        jid = self.tracker.start(spec, meta={"title": title})
        self.notify(f"已开始下载：{title}（{len(items)} 个文件）", "ok")
        self.win.downloads_page.attach(jid, title, len(items))
        return jid
