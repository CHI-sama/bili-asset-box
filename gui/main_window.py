"""main_window.py —— 主窗口：左侧导航 + 右侧页面栈。

页面关系：
  * 一级页面固定在侧栏（发现/直播/我的/下载/视频/账号/设置/反馈）；
  * 收藏集详情、装扮详情是"二级页面"，打开时压栈并显示返回按钮。
"""
from __future__ import annotations

import os
from pathlib import Path

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from .pages.assets import AssetsPage
from .pages.browse import BrowsePage
from .pages.collection import CollectionPage
from .pages.downloads import DownloadsPage
from .pages.feedback import FeedbackPage
from .pages.live import LivePage
from .pages.login import LoginPage
from .pages.settings import SettingsPage
from .pages.suit import SuitPage
from .pages.video import VideoPage
from .paths import APP_DIR
from .widgets.common import InfoBar
from .widgets.thumbs import ThumbPool

NAV = [
    ("发现", "浏览 / 搜索收藏集和装扮"),
    ("直播", "直播间表情包"),
    ("我的", "已拥有的装扮与卡片"),
    ("下载", "任务队列与进度"),
    ("视频", "解析并下载视频"),
    ("账号", "扫码登录 / Cookie"),
    ("设置", "目录、限速、ffmpeg"),
    ("反馈", "生成诊断包、提交 issue"),
]


class MainWindow(QWidget):
    """整个应用的主窗口。"""

    def __init__(self, core, settings: dict):
        super().__init__()
        self.core = core
        self.settings = settings
        self.app_version = "0.1.0"
        self.root = APP_DIR
        self.log_file = self.root / "logs" / "gui.log"
        #任务轮询器和缩略图池是全局单例，页面都从 self.win 上取
        from .core_bridge import JobTracker

        self.tracker = JobTracker(core, self)
        self.thumbs = ThumbPool(self)
        self.session_info: dict = {}

        self.setWindowTitle("B站素材盒子 · BiliAssetBox")
        #窗口自己也挂一份图标：任务栏、Alt-Tab、标题栏都用它
        from .icon import app_icon

        self.setWindowIcon(app_icon())
        self.resize(1180, 780)
        self.setMinimumSize(960, 640)

        root = QHBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        #侧栏
        side = QFrame()
        side.setObjectName("Sidebar")
        side.setFixedWidth(196)
        sl = QVBoxLayout(side)
        sl.setContentsMargins(0, 0, 0, 12)
        sl.setSpacing(0)

        logo = QLabel("BiliAssetBox")
        logo.setObjectName("Logo")
        sl.addWidget(logo)
        sub = QLabel("B 站素材下载器")
        sub.setObjectName("LogoSub")
        sl.addWidget(sub)

        self.nav_buttons: list[QPushButton] = []
        for i, (name, tip) in enumerate(NAV):
            btn = QPushButton(name)
            btn.setObjectName("NavButton")
            btn.setCheckable(True)
            btn.setCursor(Qt.PointingHandCursor)
            btn.setToolTip(tip)
            btn.clicked.connect(lambda _=False, idx=i: self.go_level1(idx))
            sl.addWidget(btn)
            self.nav_buttons.append(btn)

        sl.addStretch(1)
        self.session_label = QLabel("未登录")
        self.session_label.setObjectName("Faint")
        self.session_label.setWordWrap(True)
        self.session_label.setContentsMargins(18, 0, 12, 0)
        sl.addWidget(self.session_label)
        root.addWidget(side)

        #内容区
        content = QWidget()
        content.setObjectName("ContentArea")
        cl = QVBoxLayout(content)
        cl.setContentsMargins(24, 18, 24, 16)
        cl.setSpacing(12)

        top = QHBoxLayout()
        top.setSpacing(12)
        self.back_btn = QPushButton("← 返回")
        self.back_btn.setObjectName("Ghost")
        self.back_btn.setCursor(Qt.PointingHandCursor)
        self.back_btn.clicked.connect(self.go_back)
        self.back_btn.hide()
        top.addWidget(self.back_btn)
        #标题区：大标题 + 一行灰色说明（内容随页面变化）
        title_box = QVBoxLayout()
        title_box.setSpacing(2)
        self.title_label = QLabel("发现")
        self.title_label.setObjectName("PageTitle")
        title_box.addWidget(self.title_label)
        self.subtitle = QLabel("")
        self.subtitle.setObjectName("PageDesc")
        self.subtitle.setWordWrap(True)
        title_box.addWidget(self.subtitle)
        top.addLayout(title_box, 1)
        cl.addLayout(top)

        self.stack = QStackedWidget()
        cl.addWidget(self.stack, 1)

        self.info_bar = InfoBar()
        cl.addWidget(self.info_bar)
        root.addWidget(content, 1)

        #页面
        self.browse_page = BrowsePage(self)
        self.live_page = LivePage(self)
        self.assets_page = AssetsPage(self)
        self.downloads_page = DownloadsPage(self)
        self.video_page = VideoPage(self)
        self.login_page = LoginPage(self)
        self.settings_page = SettingsPage(self)
        self.feedback_page = FeedbackPage(self)
        self.level1_pages = [
            self.browse_page,
            self.live_page,
            self.assets_page,
            self.downloads_page,
            self.video_page,
            self.login_page,
            self.settings_page,
            self.feedback_page,
        ]
        for page in self.level1_pages:
            self.stack.addWidget(page)

        self.detail_pages: list[QWidget] = []
        self.go_level1(0)

        #启动后异步刷新登录态，别卡住界面
        QTimer.singleShot(300, self.reload_session)

    #导航
    def go_level1(self, index: int) -> None:
        #清掉之前的二级页面
        while self.detail_pages:
            page = self.detail_pages.pop()
            self.stack.removeWidget(page)
            page.deleteLater()
        for i, btn in enumerate(self.nav_buttons):
            btn.setChecked(i == index)
        page = self.level1_pages[index]
        self.stack.setCurrentWidget(page)
        self.back_btn.hide()
        self.title_label.setText(page.title)
        self.subtitle.setText(page.desc)
        #首次进入时按需加载数据
        if page is self.assets_page and not getattr(page, "_loaded", False):
            page._loaded = True
            page.load()

    def go_back(self) -> None:
        if not self.detail_pages:
            return
        page = self.detail_pages.pop()
        self.stack.removeWidget(page)
        page.deleteLater()
        if self.detail_pages:
            cur = self.detail_pages[-1]
            self.stack.setCurrentWidget(cur)
            self.title_label.setText(getattr(cur, "title", ""))
            self.subtitle.setText(getattr(cur, "desc", ""))
            self.back_btn.show()
        else:
            self.go_level1(max(0, self._current_nav()))
            self.back_btn.hide()

    def _current_nav(self) -> int:
        for i, btn in enumerate(self.nav_buttons):
            if btn.isChecked():
                return i
        return 0

    def open_suit(self, item_id: int) -> None:
        if not item_id:
            return
        page = SuitPage(self, item_id)
        self._push_detail(page, f"装扮 {item_id}")

    def open_collection(self, act_id: int, lottery_id: int) -> None:
        if not act_id:
            return
        page = CollectionPage(self, act_id, lottery_id)
        self._push_detail(page, "收藏集")

    def _push_detail(self, page: QWidget, title: str) -> None:
        self.stack.addWidget(page)
        self.detail_pages.append(page)
        self.stack.setCurrentWidget(page)
        self.title_label.setText(title)
        self.subtitle.setText(getattr(page, "desc", ""))
        self.back_btn.show()
        for btn in self.nav_buttons:
            btn.setChecked(False)

    #状态
    def set_page_title(self, page: QWidget, text: str) -> None:
        if self.stack.currentWidget() is page and text:
            self.title_label.setText(text)

    def notify(self, text: str, kind: str = "info") -> None:
        self.info_bar.show_message(text, kind)

    def reload_session(self) -> None:
        """刷新登录态（扫码成功、退出登录、启动时都会调）。"""
        try:
            nav = self.core.call("nav", {})
        except Exception as exc:
            self.session_label.setText(f"内核异常：{exc}")
            return
        self.session_info = nav
        if nav.get("logged_in"):
            self.session_label.setText(f"已登录：{nav.get('uname') or nav.get('mid')}\nUID {nav.get('mid')}")
            self.session_label.setStyleSheet("color:#3DDC97;")
        else:
            self.session_label.setText("未登录（公开内容仍可浏览下载）")
            self.session_label.setStyleSheet("color:#FFC757;")
        #登录页自己也刷新一下状态卡
        if hasattr(self.login_page, "refresh_status"):
            self.login_page.refresh_status()

    def closeEvent(self, event) -> None:  # noqa: N802 - Qt 命名
        """关窗前取消所有下载、停掉缩略图线程池，避免后台线程继续跑。"""
        try:
            self.tracker.stop_all()
        except Exception:
            pass
        try:
            self.thumbs.shutdown()
        except Exception:
            pass
        super().closeEvent(event)


def download_dir_of(settings: dict, root: Path) -> Path:
    d = settings.get("download_dir") or str(root / "downloads")
    p = Path(d)
    os.makedirs(p, exist_ok=True)
    return p
