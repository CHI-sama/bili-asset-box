"""assets.py —— 我拥有的装扮 / 收藏集卡片。

数据来自三个需要登录的接口（内核里并发调用并合并），匿名访问会返回 -101，
这里统一提示去账号页登录。
"""
from __future__ import annotations

from PySide6.QtWidgets import QLabel, QTabWidget

from ..widgets.common import api_async, primary_button
from ..widgets.grid import AssetGrid
from .base import Page


class AssetsPage(Page):
    title = "我的"
    desc = "这里列出账号里已拥有的装扮和收藏集卡片，点开就能下载整套素材。"

    def __init__(self, win, parent=None):
        super().__init__(win, parent)

        self.hint = QLabel("登录后点「刷新」加载。")
        self.hint.setObjectName("Faint")
        self.body.addWidget(self.hint)

        self.tabs = QTabWidget()
        self.suit_grid = AssetGrid(self.thumbs)
        self.suit_grid.item_activated.connect(lambda d: self.win.open_suit(int(d.get("item_id") or 0)))
        self.card_grid = AssetGrid(self.thumbs)
        self.card_grid.item_activated.connect(
            lambda d: self.win.open_collection(int(d.get("act_id") or 0), int(d.get("lottery_id") or 0))
        )
        self.tabs.addTab(self.suit_grid, "我的装扮")
        self.tabs.addTab(self.card_grid, "我的收藏集卡片")
        self.body.addWidget(self.tabs, 1)

        bar = primary_button("刷新")
        bar.clicked.connect(self.load)
        self.body.addWidget(bar)

    #加载
    def load(self) -> None:
        self.hint.setText("正在加载…")
        api_async(self.core, "my_assets", {}, self._on_ok, self._on_err)

    def _on_ok(self, res: dict) -> None:
        suits = res.get("suits", [])
        cards = res.get("collections", [])
        self.suit_grid.clear_cards()
        for s in suits:
            self.suit_grid.add_card(
                dict(s), s.get("name") or f"装扮 {s.get('item_id')}", s.get("expire", ""), s.get("cover", "")
            )
        self.card_grid.clear_cards()
        for c in cards:
            sub = f"卡号 {c.get('number')}" if c.get("number") else ""
            self.card_grid.add_card(
                dict(c), c.get("name") or f"卡片 {c.get('card_id')}", sub, c.get("image", "")
            )
        self.hint.setText(f"我的装扮 {len(suits)} 个、收藏集卡片 {len(cards)} 张。双击打开详情。")
        if not suits and not cards:
            self.hint.setText("没有查到已拥有的资产；如果确实有，可能是登录过期或接口变动。")

    def _on_err(self, code: int, msg: str) -> None:
        self.hint.setText(f"加载失败：{msg}")
        if code == -4:
            self.notify("「我的」页面需要登录，请先到「账号」页扫码", "warn")
        else:
            self.notify(f"加载失败：{msg}", "error")
