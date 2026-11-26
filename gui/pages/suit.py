"""suit.py —— 装扮详情：按分组（卡片/表情/加载动画/挂件/背景…）展示全部素材。

内核已经把 suit/detail 的嵌套结构拍平成 [{"url","name","part","kind"}]，
并用 strip_hdslb_suffix 去掉了 @ 缩放，所以这里拿到的就是原图地址。
"""
from __future__ import annotations

from collections import Counter

from PySide6.QtWidgets import QComboBox, QHBoxLayout, QLabel

from ..widgets.common import api_async, primary_button
from ..widgets.grid import AssetGrid
from .base import Page, ext_of, safe_name

PART_LABEL = {
    "card": "卡片",
    "card_bg": "卡片背景",
    "emoji_package": "表情",
    "loading": "加载动画",
    "pendant": "头像挂件",
    "play_icon": "播放图标",
    "skin": "装扮皮肤",
    "space_bg": "空间背景",
    "thumbup": "点赞动效",
}


class SuitPage(Page):
    title = "装扮"
    desc = ""

    def __init__(self, win, item_id: int, parent=None):
        super().__init__(win, parent)
        self.item_id = item_id
        self.files: list[dict] = []
        self.shown: list[dict] = []

        bar = QHBoxLayout()
        self.info = QLabel(f"item_id={item_id}，正在加载…")
        self.info.setObjectName("PageDesc")
        bar.addWidget(self.info, 1)
        self.filter = QComboBox()
        self.filter.setMinimumWidth(150)
        self.filter.currentIndexChanged.connect(self._apply_filter)
        bar.addWidget(self.filter)
        self.sel_all = primary_button("全选")
        self.sel_all.clicked.connect(lambda: self.grid.select_all(True))
        bar.addWidget(self.sel_all)
        self.dl_sel = primary_button("下载选中")
        self.dl_sel.clicked.connect(self._download_checked)
        bar.addWidget(self.dl_sel)
        self.dl_all = primary_button("全部下载")
        self.dl_all.clicked.connect(self._download_all)
        bar.addWidget(self.dl_all)
        self.body.addLayout(bar)

        self.grid = AssetGrid(self.thumbs)
        self.body.addWidget(self.grid, 1)

        self.load()

    #加载
    def load(self) -> None:
        self.dl_sel.setEnabled(False)
        self.dl_all.setEnabled(False)
        api_async(self.core, "suit_detail", {"item_id": self.item_id}, self._on_detail, self._on_error)

    def _on_detail(self, res: dict) -> None:
        self.files = res.get("files", [])
        name = res.get("name") or f"装扮 {self.item_id}"
        self.win.set_page_title(self, name)
        self.info.setText(f"{name} · 共 {len(self.files)} 个素材")
        self.filter.blockSignals(True)
        self.filter.clear()
        self.filter.addItem("全部分组", "")
        counts = Counter(f.get("part", "") for f in self.files)
        for part, cnt in counts.items():
            label = PART_LABEL.get(part, part or "其他")
            self.filter.addItem(f"{label}（{cnt}）", part)
        self.filter.blockSignals(False)
        self._apply_filter()
        self.dl_sel.setEnabled(bool(self.files))
        self.dl_all.setEnabled(bool(self.files))

    def _on_error(self, code: int, msg: str) -> None:
        self.info.setText(f"加载失败：{msg}")
        self.notify(f"装扮加载失败：{msg}", "error")

    #过滤
    def _apply_filter(self) -> None:
        part = self.filter.currentData()
        self.shown = [f for f in self.files if not part or f.get("part") == part]
        self.grid.clear_cards()
        seen: Counter = Counter()
        for f in self.shown:
            key = f.get("part", "")
            seen[key] += 1
            label = PART_LABEL.get(key, key or "其他")
            #内核给的是文件名（card_xxx.png），界面上换成"卡片 1"这种更好认，
            #原始文件名放在提示里，下载后还是按原名落盘。
            title = f"{label} {seen[key]}"
            ext = ext_of(f.get("url", "")).lstrip(".").upper()
            data = dict(f)
            self.grid.add_card(data, title, ext, f.get("url", ""))

    #下载
    def _to_files(self, rows: list[dict]) -> list[dict]:
        out = []
        for f in rows:
            url = f.get("url", "")
            if not url:
                continue
            part = f.get("part") or "other"
            name = safe_name(f.get("name") or part)
            out.append(
                {
                    "url": url,
                    "name": f"suit_{self.item_id}/{part}_{name}{ext_of(url)}",
                }
            )
        return out

    def _download_checked(self) -> None:
        rows = self.grid.checked_items() or self.shown
        if not rows:
            return
        if not self.grid.checked_items():
            self.notify("没有勾选，默认下载当前分组全部", "info")
        self.start_files(self._to_files(rows), f"装扮 {self.item_id}（{len(rows)} 个素材）")

    def _download_all(self) -> None:
        if not self.files:
            return
        self.start_files(self._to_files(self.files), f"装扮 {self.item_id}（全部 {len(self.files)} 个素材）")
