"""login.py —— 扫码登录 / 手动粘贴 Cookie / 退出登录。

二维码接口返回的是一个 URL 字符串，这里用 qrcode 库把它画成 PNG，
再交给 QPixmap 显示，不依赖任何额外的 Qt 插件。
"""
from __future__ import annotations

import io

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QVBoxLayout,
    QWidget,
)

from ..widgets.common import api_async, card, primary_button
from .base import Page


class LoginPage(Page):
    title = "账号"
    desc = "扫码登录后就能下载「我拥有的装扮/收藏集」、直播间表情，以及高清晰度视频。"

    def __init__(self, win, parent=None):
        super().__init__(win, parent)
        self._qr_key = ""
        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(2200)
        self._poll_timer.timeout.connect(self._poll_once)

        # ---- 状态卡
        status_frame, sl = card()
        row = QHBoxLayout()
        self.avatar = QLabel("未登录")
        self.avatar.setObjectName("SectionTitle")
        row.addWidget(self.avatar, 1)
        self.state_label = QLabel("")
        self.state_label.setObjectName("Faint")
        row.addWidget(self.state_label)
        sl.addLayout(row)
        self.detail = QLabel("登录状态会加密保存在本机（Windows DPAPI），只对当前用户可解。")
        self.detail.setObjectName("Faint")
        self.detail.setWordWrap(True)
        sl.addWidget(self.detail)
        self.body.addWidget(status_frame)

        # ---- 扫码卡
        qr_frame, ql = card()
        ql.addWidget(QLabel("扫码登录"))
        self.qr_label = QLabel("点击下方按钮生成二维码")
        self.qr_label.setAlignment(Qt.AlignCenter)
        self.qr_label.setMinimumSize(230, 230)
        self.qr_label.setStyleSheet("background: #FFFFFF; border-radius: 10px; color: #666;")
        ql.addWidget(self.qr_label, 0, Qt.AlignHCenter)
        btn_row = QHBoxLayout()
        self.qr_btn = primary_button("生成二维码")
        self.qr_btn.clicked.connect(self._start_qr)
        btn_row.addWidget(self.qr_btn)
        self.qr_hint = QLabel("用 B 站手机客户端扫一扫")
        self.qr_hint.setObjectName("Faint")
        btn_row.addWidget(self.qr_hint, 1)
        ql.addLayout(btn_row)
        self.body.addWidget(qr_frame)

        # ---- 手动 Cookie 卡
        ck_frame, cl = card()
        cl.addWidget(QLabel("或手动粘贴 Cookie"))
        tip = QLabel("浏览器 F12 → Network → 任意 api.bilibili.com 请求 → 复制请求头里的整段 Cookie。")
        tip.setObjectName("Faint")
        tip.setWordWrap(True)
        cl.addWidget(tip)
        self.cookie_edit = QLineEdit()
        self.cookie_edit.setPlaceholderText("SESSDATA=...; bili_jct=...; DedeUserID=...")
        cl.addWidget(self.cookie_edit)
        crow = QHBoxLayout()
        set_btn = primary_button("保存并验证")
        set_btn.clicked.connect(self._save_cookie)
        crow.addWidget(set_btn)
        self.logout_btn = primary_button("退出登录")
        self.logout_btn.clicked.connect(self._logout)
        crow.addWidget(self.logout_btn)
        crow.addStretch(1)
        cl.addLayout(crow)
        self.body.addWidget(ck_frame)
        self.body.addStretch(1)

        self.refresh_status()

    #状态
    def refresh_status(self) -> None:
        info = self.win.session_info or {}
        if info.get("logged_in"):
            name = info.get("uname") or f"UID {info.get('mid')}"
            self.avatar.setText(name)
            self.state_label.setText("已登录")
            self.state_label.setStyleSheet("color: #3DDC97;")
            self.logout_btn.setEnabled(True)
        else:
            self.avatar.setText("未登录")
            self.state_label.setText("仅能浏览公开内容")
            self.state_label.setStyleSheet("color: #9AA3B2;")
            self.logout_btn.setEnabled(False)

    #扫码
    def _start_qr(self) -> None:
        self.qr_btn.setEnabled(False)
        self.qr_hint.setText("正在申请二维码…")
        api_async(self.core, "login_qr_generate", {}, self._on_qr, self._on_qr_err)

    def _on_qr(self, res: dict) -> None:
        self._qr_key = res.get("qrcode_key", "")
        url = res.get("url", "")
        pix = _qr_pixmap(url)
        if pix is None:
            self.qr_hint.setText("二维码生成失败，请改用手动 Cookie")
            self.qr_btn.setEnabled(True)
            return
        self.qr_label.setPixmap(pix.scaled(220, 220, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        self.qr_hint.setText("请用手机扫码，扫完在手机上确认")
        self._poll_timer.start()

    def _on_qr_err(self, code: int, msg: str) -> None:
        self.qr_btn.setEnabled(True)
        self.qr_hint.setText(f"生成失败：{msg}")
        self.notify(f"生成二维码失败：{msg}", "error")

    def _poll_once(self) -> None:
        if not self._qr_key:
            self._poll_timer.stop()
            return
        api_async(self.core, "login_qr_poll", {"qrcode_key": self._qr_key}, self._on_poll, self._on_poll_err)

    def _on_poll(self, res: dict) -> None:
        state = res.get("state", -1)
        if state == 0:
            self._poll_timer.stop()
            self.qr_btn.setEnabled(True)
            self.qr_hint.setText("登录成功")
            self.qr_label.setText("✅")
            self.win.reload_session()
            self.notify("登录成功，已保存登录状态", "ok")
        elif state == 86090:
            self.qr_hint.setText("已扫码，请在手机上确认")
        elif state == 86038:
            self._poll_timer.stop()
            self.qr_btn.setEnabled(True)
            self.qr_hint.setText("二维码已过期，请重新生成")
        else:
            self.qr_hint.setText("等待扫码…")

    def _on_poll_err(self, code: int, msg: str) -> None:
        self._poll_timer.stop()
        self.qr_btn.setEnabled(True)
        self.qr_hint.setText(f"轮询失败：{msg}")

    #Cookie
    def _save_cookie(self) -> None:
        text = self.cookie_edit.text().strip()
        if not text:
            self.notify("先粘贴 Cookie 再保存", "warn")
            return
        api_async(self.core, "login_cookie_set", {"cookie": text}, self._on_cookie, self._on_cookie_err)

    def _on_cookie(self, res: dict) -> None:
        self.cookie_edit.clear()
        self.win.reload_session()
        self.notify("Cookie 验证通过，登录成功", "ok")

    def _on_cookie_err(self, code: int, msg: str) -> None:
        self.notify(f"Cookie 无效：{msg}", "error")

    def _logout(self) -> None:
        api_async(self.core, "logout", {}, lambda _: (self.win.reload_session(), self.notify("已退出登录", "info")))


def _qr_pixmap(text: str) -> QPixmap | None:
    """用 qrcode 把 URL 画成位图。失败返回 None（不抛异常，界面不能崩）。"""
    try:
        import qrcode

        qr = qrcode.QRCode(border=2, box_size=8)
        qr.add_data(text)
        qr.make(fit=True)
        img = qr.make_image(fill_color="#111111", back_color="#FFFFFF").convert("RGB")
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        pix = QPixmap()
        pix.loadFromData(buf.getvalue())
        return pix
    except Exception:
        return None
