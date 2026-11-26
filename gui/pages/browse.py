"""browse.py —— 首页：按关键词搜索装扮 / 收藏集。

搜索结果里收藏集和装扮混在一起，这里按 kind 区分：双击收藏集进卡片列表，
双击装扮进素材列表。
"""
from __future__ import annotations

from PySide6.QtWidgets import QComboBox, QHBoxLayout, QLabel, QLineEdit

from ..widgets.common import api_async, primary_button
from ..widgets.grid import AssetGrid
from .base import Page


class BrowsePage(Page):
    title = "发现"
    desc = "搜索装扮或收藏集；点开一项就能看到全部素材并一键下载原图。"

    def __init__(self, win, parent=None):
        super().__init__(win, parent)

        bar = QHBoxLayout()
        self.input = QLineEdit()
        self.input.setPlaceholderText("输入关键词，例如：初音未来、洛天依、东方project……")
        self.input.returnPressed.connect(self.search)
        bar.addWidget(self.input, 1)
        self.kind = QComboBox()
        self.kind.addItems(["全部", "只看收藏集", "只看装扮"])
        bar.addWidget(self.kind)
        self.btn = primary_button("搜索")
        self.btn.clicked.connect(self.search)
        bar.addWidget(self.btn)
        self.body.addLayout(bar)

        self.hint = QLabel("支持 B 站装扮/收藏集搜索接口，结果来自官方商城。")
        self.hint.setObjectName("Faint")
        self.body.addWidget(self.hint)

        self.grid = AssetGrid(self.thumbs)
        self.grid.item_activated.connect(self._open)
        self.body.addWidget(self.grid, 1)

    #搜索
    def search(self) -> None:
        kw = self.input.text().strip()
        if not kw:
            self.notify("先输入关键词", "warn")
            return
        self.btn.setEnabled(False)
        self.hint.setText(f"正在搜索「{kw}」…")
        api_async(self.core, "garb_search", {"keyword": kw, "pn": 1, "ps": 30}, self._on_result, self._on_error)

    def _on_result(self, res: dict) -> None:
        self.btn.setEnabled(True)
        items = res.get("items", [])
        mode = self.kind.currentIndex()
        self.grid.clear_cards()
        shown = 0
        for it in items:
            kind = it.get("kind")
            if mode == 1 and kind != "collection":
                continue
            if mode == 2 and kind != "suit":
                continue
            shown += 1
            title = it.get("name") or "未命名"
            sub = "收藏集" if kind == "collection" else "装扮"
            price = it.get("price") or 0
            if price:
                sub += f" · ¥{price / 100:.2f}"
            data = dict(it)
            data["_kind"] = kind
            self.grid.add_card(data, title, sub, it.get("cover", ""))
        self.hint.setText(f"共 {len(items)} 条结果，显示 {shown} 条；双击卡片查看详情。")
        if shown == 0:
            self.hint.setText("没有结果，换个关键词试试。")

    def _on_error(self, code: int, msg: str) -> None:
        self.btn.setEnabled(True)
        self.hint.setText(f"搜索失败：{msg}")
        self.notify(f"搜索失败：{msg}", "error")

    #打开详情
    def _open(self, data: dict) -> None:
        if data.get("_kind") == "collection" or (data.get("act_id") and not data.get("item_id")):
            self.win.open_collection(int(data.get("act_id") or 0), int(data.get("lottery_id") or 0))
        else:
            self.win.open_suit(int(data.get("item_id") or 0))
