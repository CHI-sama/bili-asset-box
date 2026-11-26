"""video.py —— 视频下载：解析 BV/av/链接，选分P和清晰度，DASH 合并成 mp4。

清晰度只是"上限"：内核拿到 playurl 后会挑 ≤ 该清晰度的最好一档，普通账号
最高一般就是 1080P（qn=80），大会员才有更高。
"""
from __future__ import annotations

from PySide6.QtWidgets import QComboBox, QHBoxLayout, QLabel, QLineEdit

from ..widgets.common import api_async, card, primary_button
from .base import Page, safe_name

QUALITY = [
    ("自动（能拿到的最高）", 120),
    ("1080P 高码率", 112),
    ("1080P", 80),
    ("720P", 64),
    ("480P", 32),
]


class VideoPage(Page):
    title = "视频"
    desc = "支持 BV 号、av 号或完整链接；音视频分别下载后用 ffmpeg 无损合并。"

    def __init__(self, win, parent=None):
        super().__init__(win, parent)
        self.info: dict = {}
        self.pages: list[dict] = []

        bar = QHBoxLayout()
        self.input = QLineEdit()
        self.input.setPlaceholderText("粘贴 BV1xx411c7mD / av170001 / https://www.bilibili.com/video/...")
        self.input.returnPressed.connect(self.parse)
        bar.addWidget(self.input, 1)
        self.btn = primary_button("解析")
        self.btn.clicked.connect(self.parse)
        bar.addWidget(self.btn)
        self.body.addLayout(bar)

        frame, fl = card()
        self.title_label = QLabel("还没有解析视频")
        self.title_label.setObjectName("SectionTitle")
        self.title_label.setWordWrap(True)
        fl.addWidget(self.title_label)
        self.meta = QLabel("")
        self.meta.setObjectName("Faint")
        self.meta.setWordWrap(True)
        fl.addWidget(self.meta)

        row = QHBoxLayout()
        row.addWidget(QLabel("分P"))
        self.page_combo = QComboBox()
        self.page_combo.setMinimumWidth(260)
        row.addWidget(self.page_combo, 1)
        row.addWidget(QLabel("清晰度"))
        self.q_combo = QComboBox()
        self.q_combo.setMinimumWidth(150)
        for label, qn in QUALITY:
            self.q_combo.addItem(label, qn)
        row.addWidget(self.q_combo)
        self.dl_btn = primary_button("下载")
        self.dl_btn.clicked.connect(self.download)
        self.dl_btn.setEnabled(False)
        row.addWidget(self.dl_btn)
        fl.addLayout(row)
        self.body.addWidget(frame)

        self.hint = QLabel("提示：需要 ffmpeg 才能合并音视频；设置页可以指定路径。")
        self.hint.setObjectName("Faint")
        self.body.addWidget(self.hint)
        self.body.addStretch(1)

    #解析
    def parse(self) -> None:
        text = self.input.text().strip()
        if not text:
            self.notify("先粘贴视频链接或 BV 号", "warn")
            return
        self.btn.setEnabled(False)
        self.title_label.setText("正在解析…")
        api_async(self.core, "video_info", {"input": text}, self._on_info, self._on_err)

    def _on_info(self, res: dict) -> None:
        self.btn.setEnabled(True)
        self.info = res
        self.pages = res.get("pages", [])
        self.title_label.setText(res.get("title") or "未命名视频")
        mins = (res.get("duration") or 0) // 60
        self.meta.setText(f"{res.get('bvid')} · {res.get('owner')} · {len(self.pages)} P · {mins} 分钟")
        self.page_combo.clear()
        for p in self.pages:
            self.page_combo.addItem(f"P{p.get('page')} {p.get('title') or ''}".strip(), p.get("cid"))
        self.dl_btn.setEnabled(bool(self.pages))

    def _on_err(self, code: int, msg: str) -> None:
        self.btn.setEnabled(True)
        self.title_label.setText("解析失败")
        self.meta.setText(msg)
        self.notify(f"解析失败：{msg}", "error")

    #下载
    def download(self) -> None:
        cid = self.page_combo.currentData()
        if not cid:
            self.notify("先选择一个分P", "warn")
            return
        qn = self.q_combo.currentData()
        title = self.info.get("title") or "视频"
        fname = safe_name(title, 70)
        out_dir = str(self.download_dir() / "videos")
        spec = {
            "type": "video",
            "out_dir": out_dir,
            "bvid": self.info.get("bvid", ""),
            "cid": int(cid),
            "qn": int(qn),
            "name": fname,
            "ffmpeg": self.settings.get("ffmpeg", ""),
            "title": fname,
        }
        try:
            jid = self.tracker.start(spec, meta={"title": fname})
        except Exception as exc:  #比如 ffmpeg 没找到
            self.notify(f"无法开始下载：{exc}", "error")
            return
        self.win.downloads_page.attach(jid, fname, 2)
        self.notify(f"已开始下载：{fname}", "ok")
