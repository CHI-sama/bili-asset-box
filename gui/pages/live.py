"""live.py —— 直播间表情。

接口要求登录（未登录返回 -101），所以这里失败时直接提示去登录页。
每个包里的表情都是 i0.hdslb.com 的 png/gif，去掉 @ 后缀就是原尺寸。
"""
from __future__ import annotations

from PySide6.QtWidgets import QComboBox, QHBoxLayout, QLabel, QLineEdit

from ..widgets.common import api_async, primary_button
from ..widgets.grid import AssetGrid
from .base import Page, ext_of, safe_name


class LivePage(Page):
    title = "直播表情"
    desc = "输入直播间房号，拉取该房间可用的表情包；需要登录后才能看到完整表情。"

    def __init__(self, win, parent=None):
        super().__init__(win, parent)
        self.packages: list[dict] = []
        self.room = ""

        bar = QHBoxLayout()
        self.room_edit = QLineEdit(str(self.settings.get("last_room", "")))
        self.room_edit.setPlaceholderText("直播间房号，例如 5424")
        self.room_edit.returnPressed.connect(self.load)
        bar.addWidget(self.room_edit, 1)
        self.btn = primary_button("拉取表情")
        self.btn.clicked.connect(self.load)
        bar.addWidget(self.btn)
        bar.addWidget(QLabel("分组"))
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

        self.hint = QLabel("")
        self.hint.setObjectName("Faint")
        self.body.addWidget(self.hint)

        self.grid = AssetGrid(self.thumbs)
        self.body.addWidget(self.grid, 1)

    #加载
    def load(self) -> None:
        room = self.room_edit.text().strip()
        if not room.isdigit():
            self.notify("房号必须是数字（短号也能用，接口会自己解析）", "warn")
            return
        self.room = room
        self.btn.setEnabled(False)
        self.hint.setText("正在拉取表情包…")
        api_async(self.core, "live_emoticons", {"room_id": room}, self._on_ok, self._on_err)

    def _on_ok(self, res: dict) -> None:
        self.btn.setEnabled(True)
        self.packages = res.get("packages", [])
        self.settings["last_room"] = self.room
        from ..core_bridge import save_settings

        save_settings(self.settings)

        self.filter.blockSignals(True)
        self.filter.clear()
        self.filter.addItem("全部表情", -1)
        total = 0
        for i, p in enumerate(self.packages):
            n = len(p.get("emoticons", []))
            total += n
            self.filter.addItem(f"{p.get('name') or '未命名'}（{n}）", i)
        self.filter.blockSignals(False)
        self._apply_filter()
        self.hint.setText(f"房间 {self.room} 共 {len(self.packages)} 个表情包、{total} 个表情。")
        if total == 0:
            self.hint.setText("没有拿到表情：可能该房间没有专属表情，或登录已过期。")

    def _on_err(self, code: int, msg: str) -> None:
        self.btn.setEnabled(True)
        self.hint.setText(f"拉取失败：{msg}")
        if code == -4:
            self.notify("直播表情需要登录，请先到「账号」页扫码", "warn")
        else:
            self.notify(f"拉取失败：{msg}", "error")

    #过滤
    def _apply_filter(self) -> None:
        idx = self.filter.currentData()
        self.grid.clear_cards()
        pkgs = self.packages if idx in (None, -1) else [self.packages[idx]]
        for p in pkgs:
            pkg_name = safe_name(p.get("name") or f"pkg_{p.get('pkg_id')}")
            for e in p.get("emoticons", []):
                url = e.get("url", "")
                if not url:
                    continue
                data = dict(e)
                data["_pkg"] = pkg_name
                sub = f"{pkg_name} · {e.get('width', '')}×{e.get('height', '')}"
                self.grid.add_card(data, e.get("text") or pkg_name, sub, url)

    #下载
    def _to_files(self, rows: list[dict]) -> list[dict]:
        out = []
        for e in rows:
            url = e.get("url", "")
            if not url:
                continue
            pkg = e.get("_pkg") or "emoticons"
            name = safe_name(e.get("text") or e.get("unique") or f"emote_{e.get('id')}")
            out.append({"url": url, "name": f"live_{self.room}/{pkg}/{name}{ext_of(url)}"})
        return out

    def _download_checked(self) -> None:
        rows = self.grid.checked_items()
        if not rows:
            self.notify("先勾选要下载的表情", "warn")
            return
        self.start_files(self._to_files(rows), f"直播表情 {self.room}（{len(rows)} 个）")

    def _download_all(self) -> None:
        rows = []
        for p in self.packages:
            rows.extend(p.get("emoticons", []))
        if not rows:
            return
        #全量下载时补上包名
        mapped = []
        for p in self.packages:
            pkg = safe_name(p.get("name") or f"pkg_{p.get('pkg_id')}")
            for e in p.get("emoticons", []):
                e = dict(e)
                e["_pkg"] = pkg
                mapped.append(e)
        self.start_files(self._to_files(mapped), f"直播表情 {self.room}（全部 {len(mapped)} 个）")
