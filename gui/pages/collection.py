"""collection.py —— 收藏集详情：卡片原图 + 动态卡视频。

收藏集接口返回的 card_img 是**无水印原图**（card_img_download 才带水印），
所以这里一律用 card_img，并去掉 hdslb 的 @ 缩放后缀，保证是最清晰的那份。
"""
from __future__ import annotations

from pathlib import Path

from PySide6.QtWidgets import QHBoxLayout, QLabel

from ..widgets.common import api_async, primary_button
from ..widgets.grid import AssetGrid
from .base import Page, ext_of, safe_name


class CollectionPage(Page):
    title = "收藏集"
    desc = ""

    def __init__(self, win, act_id: int, lottery_id: int = 0, parent=None):
        super().__init__(win, parent)
        self.act_id = act_id
        self.lottery_id = lottery_id
        self.cards: list[dict] = []

        bar = QHBoxLayout()
        self.info = QLabel(f"act_id={act_id}，正在加载…")
        self.info.setObjectName("PageDesc")
        bar.addWidget(self.info, 1)
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
        api_async(
            self.core,
            "collection_detail",
            {"act_id": self.act_id, "lottery_id": self.lottery_id},
            self._on_detail,
            self._on_error,
        )

    def _on_detail(self, res: dict) -> None:
        self.cards = res.get("cards", [])
        name = res.get("name") or f"收藏集 {self.act_id}"
        self.info.setText(f"{name} · {len(self.cards)} 张卡片（原图）")
        self.win.set_page_title(self, name)
        self.grid.clear_cards()
        for c in self.cards:
            sub = "动态卡" if c.get("is_dynamic") else "静态卡"
            if c.get("scarcity"):
                sub += f" · 稀有度 {c['scarcity']}"
            data = dict(c)
            data["_folder"] = f"collection_{self.act_id}"
            self.grid.add_card(data, c.get("name") or "未命名卡片", sub, c.get("img", ""))
        self.dl_sel.setEnabled(bool(self.cards))
        self.dl_all.setEnabled(bool(self.cards))

    def _on_error(self, code: int, msg: str) -> None:
        self.info.setText(f"加载失败：{msg}")
        self.notify(f"收藏集加载失败：{msg}", "error")

    #下载
    def _files_for(self, cards: list[dict]) -> list[dict]:
        out = []
        for c in cards:
            folder = c.get("_folder", f"collection_{self.act_id}")
            base = safe_name(c.get("name") or f"card_{c.get('card_id', '')}")
            img = c.get("img", "")
            if img:
                out.append({"url": img, "name": f"{folder}/{base}{ext_of(img)}"})
            for i, v in enumerate(c.get("videos", [])):
                suffix = "" if i == 0 else f"_{i + 1}"
                out.append({"url": v, "name": f"{folder}/{base}_动态{suffix}.mp4"})
        return out

    def _download_checked(self) -> None:
        checked = self.grid.checked_items()
        if not checked:
            self.notify("先勾选要下载的卡片", "warn")
            return
        self.start_files(self._files_for(checked), f"收藏集 {self.act_id}（选中 {len(checked)} 张）")

    def _download_all(self) -> None:
        if not self.cards:
            return
        self.start_files(self._files_for(self.cards), f"收藏集 {self.act_id}（全部 {len(self.cards)} 张）")
