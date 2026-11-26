"""grid.py —— 素材网格（卡片列表）。

QListWidget 的 IconMode 已经能自动换行、选中、滚动，比手写 QListView+delegate
少一半代码；每张卡片的数据都塞在 item 的 UserRole 里，双击时直接取出来用。
"""
from __future__ import annotations

from PySide6.QtCore import QSize, Qt, Signal
from PySide6.QtGui import QIcon, QPixmap
from PySide6.QtWidgets import QListWidget, QListWidgetItem

from .thumbs import IMAGE_EXT, ThumbPool, ext_of_url, placeholder_image

THUMB = 150
#卡片：上方 150px 缩略图 + 下方两行文字
CARD_W = THUMB + 34
CARD_H = THUMB + 92


class AssetGrid(QListWidget):
    """可复用的素材网格。

    data 里建议放：name/url/size/part/kind… 由各页面自己决定。
    """

    item_activated = Signal(dict)   #双击 / 回车
    item_checked = Signal(dict, bool)  #勾选状态变化（用于批量下载）

    def __init__(self, thumbs: ThumbPool, columns_hint: int = 5, parent=None):
        super().__init__(parent)
        self.thumbs = thumbs
        self.setObjectName("AssetGrid")
        self.setViewMode(QListWidget.IconMode)
        self.setResizeMode(QListWidget.Adjust)
        self.setMovement(QListWidget.Static)
        self.setIconSize(QSize(THUMB, THUMB))
        self.setGridSize(QSize(CARD_W, CARD_H))
        self.setSpacing(2)
        self.setViewportMargins(4, 4, 12, 4)
        self.setWordWrap(True)
        self.setUniformItemSizes(True)
        self.setSelectionMode(QListWidget.ExtendedSelection)
        self.setVerticalScrollMode(QListWidget.ScrollPerPixel)
        self.itemDoubleClicked.connect(self._on_double)
        self.itemChanged.connect(self._on_check)
        self.thumbs.image_ready.connect(self._on_image)
        self._items_by_url: dict[str, list[QListWidgetItem]] = {}

    #添加
    def add_card(self, data: dict, title: str, subtitle: str = "", image_url: str = "") -> QListWidgetItem:
        item = QListWidgetItem()
        item.setText(f"{title}\n{subtitle}" if subtitle else title)
        item.setToolTip(title + (f"\n{subtitle}" if subtitle else ""))
        item.setData(Qt.UserRole, data)
        item.setSizeHint(QSize(CARD_W - 8, CARD_H - 8))
        item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
        item.setCheckState(Qt.Unchecked)
        item.setTextAlignment(Qt.AlignHCenter | Qt.AlignTop)
        self.addItem(item)
        ext = ext_of_url(image_url)
        if image_url and ext in IMAGE_EXT:
            #图片：异步加载，加载好之前先放占位块
            item.setIcon(QIcon(QPixmap.fromImage(placeholder_image("…", THUMB))))
            self._items_by_url.setdefault(image_url, []).append(item)
            img = self.thumbs.get(image_url)
            if img is not None:
                item.setIcon(QIcon(QPixmap.fromImage(img)))
        elif image_url:
            # json/bin/zip/mp4 之类没法当图显示，用扩展名占位
            item.setIcon(QIcon(QPixmap.fromImage(placeholder_image(ext.lstrip(".").upper() or "FILE", THUMB))))
        return item

    def _on_image(self, url: str, image) -> None:
        for item in self._items_by_url.pop(url, []):
            item.setIcon(QIcon(QPixmap.fromImage(image)))

    def _on_double(self, item: QListWidgetItem) -> None:
        data = item.data(Qt.UserRole) or {}
        if data:
            self.item_activated.emit(dict(data))

    def _on_check(self, item: QListWidgetItem) -> None:
        data = item.data(Qt.UserRole) or {}
        if data:
            self.item_checked.emit(dict(data), item.checkState() == Qt.Checked)

    #工具
    def checked_items(self) -> list[dict]:
        out = []
        for i in range(self.count()):
            it = self.item(i)
            if it.checkState() == Qt.Checked:
                out.append(dict(it.data(Qt.UserRole) or {}))
        return out

    def select_all(self, checked: bool = True) -> None:
        self.blockSignals(True)
        state = Qt.Checked if checked else Qt.Unchecked
        for i in range(self.count()):
            self.item(i).setCheckState(state)
        self.blockSignals(False)

    def clear_cards(self) -> None:
        self.clear()
        self._items_by_url.clear()
