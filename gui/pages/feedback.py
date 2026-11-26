"""feedback.py —— 反馈问题。

不自动上传任何东西：点「生成诊断包」只在本地打一个 zip（日志已截断+脱敏），
然后你可以选择打开目录自己发，或者点「打开 GitHub 提交」在浏览器里预填标题和正文。
"""
from __future__ import annotations

import os
from urllib.parse import quote

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QCheckBox, QHBoxLayout, QLabel, QLineEdit, QPlainTextEdit

from ..core_bridge import save_settings
from ..widgets.common import api_async, card, primary_button
from .base import Page

#默认反馈仓库（作者的真实仓库）；用户也可以在设置页覆盖。
DEFAULT_REPO = "https://github.com/CHI-sama/bili-asset-box"


def repo_url(settings: dict) -> str:
    """把设置里的仓库地址规整成 https://github.com/<owner>/<repo> 形式。"""
    raw = str(settings.get("feedback_repo") or "").strip() or DEFAULT_REPO
    raw = raw.rstrip("/")
    if raw.endswith(".git"):
        raw = raw[:-4]
    if not raw.startswith("http"):
        raw = "https://github.com/" + raw.lstrip("/")
    return raw


def issue_url(settings: dict, title: str, body: str) -> str:
    """仓库地址指向某个仓库就拼 issues/new，指向用户主页就原样打开。"""
    base = repo_url(settings)
    parts = [p for p in base.split("github.com/", 1)[-1].split("/") if p]
    if len(parts) >= 2:
        return f"{base}/issues/new?title={quote(title)}&body={quote(body)}"
    return base


class FeedbackPage(Page):
    title = "反馈"
    desc = "遇到问题或者有好点子，都可以在这里告诉我们哦：先写两句，生成一个诊断包，再一起去 GitHub 提 issue，就最省事啦。"

    def __init__(self, win, parent=None):
        super().__init__(win, parent)

        frame, lay = card()
        lay.addWidget(self._label("遇到的问题 / 想要的功能"))
        self.note = QPlainTextEdit()
        self.note.setPlaceholderText(
            "可以简单说说你做了什么、期待发生什么、实际又发生了什么；如果有报错文本，一起贴上就更好啦。"
        )
        self.note.setMinimumHeight(120)
        lay.addWidget(self.note)

        lay.addWidget(self._label("怎么联系你（可选哦，会一起写进 zip 里的 contact.txt）"))
        self.contact = QLineEdit(str(self.settings.get("contact", "")))
        self.contact.setPlaceholderText("邮箱 / 站内 ID / 其他能找到你的方式，都可以")
        lay.addWidget(self.contact)

        self.with_log = QCheckBox("顺便附上应用日志吧（只取最后 256KB，Cookie 和 token 会自动抹掉）")
        self.with_log.setChecked(True)
        lay.addWidget(self.with_log)

        row = QHBoxLayout()
        gen = primary_button("生成诊断包")
        gen.clicked.connect(self.generate)
        row.addWidget(gen)
        self.open_issue = primary_button("去 GitHub 提交")
        self.open_issue.clicked.connect(self.open_github)
        row.addWidget(self.open_issue)
        row.addStretch(1)
        lay.addLayout(row)

        self.target = QLabel("")
        self.target.setObjectName("Faint")
        self.target.setTextInteractionFlags(Qt.TextSelectableByMouse)
        lay.addWidget(self.target)
        self.refresh_target()

        self.result = QLabel("")
        self.result.setObjectName("Faint")
        self.result.setWordWrap(True)
        self.result.setTextInteractionFlags(Qt.TextSelectableByMouse)
        lay.addWidget(self.result)
        self.body.addWidget(frame)

        tip = QLabel(
            "诊断包里会有：应用/内核/Python 版本、系统信息、脱敏后的设置，以及（可选）日志尾部；"
            "不会带你的 Cookie 明文，也不会自动上传到任何地方哦。"
        )
        tip.setObjectName("Faint")
        tip.setWordWrap(True)
        self.body.addWidget(tip)
        self.body.addStretch(1)
        self.last_zip = ""

    #生成
    def generate(self) -> None:
        note = self.note.toPlainText().strip()
        if not note:
            self.notify("可以先写两句问题描述嘛，这样更好判断～", "warn")
            return
        contact = self.contact.text().strip()
        self.settings["contact"] = contact
        save_settings(self.settings)
        args = {
            "output_dir": str(self.download_dir().parent / "logs" / "feedback"),
            "note": note,
            "contact": contact,
            "app_version": getattr(self.win, "app_version", "0.1.0"),
            "core_version": self.core.version(),
            "python_version": _pyver(),
            "gui_log": str(self.win.log_file) if self.with_log.isChecked() else "",
            "settings": {k: v for k, v in self.settings.items() if k != "cookie"},
        }
        self.result.setText("正在打包…")
        api_async(self.core, "feedback_bundle", args, self._on_ok, self._on_err)

    def _on_ok(self, res: dict) -> None:
        self.last_zip = res.get("zip", "")
        size = res.get("size", 0) / 1024.0
        self.result.setText(f"诊断包放好啦：{self.last_zip}（{size:.0f} KB，{res.get('entries', 0)} 个文件），拖进 issue 就行")
        self.notify("诊断包已生成", "ok")

    def _on_err(self, code: int, msg: str) -> None:
        self.result.setText(f"生成失败：{msg}")
        self.notify(f"生成失败：{msg}", "error")

    #提交目标
    def refresh_target(self) -> None:
        base = repo_url(self.settings)
        self.target.setText(f"会提交到：{base}（想换成自己的仓库，可以在「设置 → 反馈仓库」里改哦）")

    def showEvent(self, event) -> None:  # noqa: N802 - Qt 的命名风格
        super().showEvent(event)
        self.refresh_target()

    #提交
    def open_github(self) -> None:
        note = self.note.toPlainText().strip()
        if not note:
            self.notify("先写两句问题描述，再打开提交页就更好啦", "warn")
            return
        body = note
        if self.last_zip:
            body += f"\n\n诊断包：{os.path.basename(self.last_zip)}（请把 zip 拖到 issue 里）"
        body += f"\n\n---\n版本：{self.core.version()} / Python {_pyver()}"
        url = issue_url(self.settings, note.splitlines()[0][:80], body)
        try:
            self.core.call("open_url", {"url": url})
            self.notify("已在浏览器打开", "ok")
        except Exception as exc:
            self.result.setText(f"打开失败：{exc}")

    #小工具
    @staticmethod
    def _label(text: str) -> QLabel:
        lab = QLabel(text)
        lab.setObjectName("SectionTitle")
        return lab


def _pyver() -> str:
    import platform

    return platform.python_version()
