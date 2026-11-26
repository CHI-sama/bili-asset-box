"""settings.py —— 设置页。

改动分两类：
  * 下载目录 / 并发 / ffmpeg 路径：只写到 settings.json，下次新建任务时生效；
  * 限速三项：立刻通过 `rate_configure` 推给内核，正在跑的任务也会跟着变。
"""
from __future__ import annotations

import os
import subprocess

from PySide6.QtWidgets import QFileDialog, QFormLayout, QHBoxLayout, QLabel, QLineEdit, QSpinBox, QVBoxLayout

from ..core_bridge import save_settings
from ..widgets.common import card, primary_button
from .base import Page


class SettingsPage(Page):
    title = "设置"
    desc = "下载目录、限速、ffmpeg 路径都在这里；改完记得点「保存」。"

    def __init__(self, win, parent=None):
        super().__init__(win, parent)

        #路径
        frame, lay = card()
        form = QFormLayout()
        form.setLabelAlignment(form.labelAlignment())

        self.dir_edit = QLineEdit(str(self.settings.get("download_dir", "")))
        row = QHBoxLayout()
        row.addWidget(self.dir_edit, 1)
        pick = primary_button("选择…")
        pick.clicked.connect(self._pick_dir)
        row.addWidget(pick)
        open_btn = primary_button("打开")
        open_btn.clicked.connect(lambda: self._open_folder(self.dir_edit.text()))
        row.addWidget(open_btn)
        form.addRow("下载目录", self._wrap(row))

        self.ffmpeg_edit = QLineEdit(str(self.settings.get("ffmpeg", "")))
        row2 = QHBoxLayout()
        row2.addWidget(self.ffmpeg_edit, 1)
        pick2 = primary_button("选择…")
        pick2.clicked.connect(self._pick_ffmpeg)
        row2.addWidget(pick2)
        form.addRow("ffmpeg 路径", self._wrap(row2))
        lay.addLayout(form)

        tip = QLabel("ffmpeg 留空表示让内核自动查找 PATH 和 %LOCALAPPDATA%\\ffmpeg\\bin。")
        tip.setObjectName("Faint")
        lay.addWidget(tip)

        #反馈仓库：决定「反馈」页的 GitHub 按钮跳到哪
        form3 = QFormLayout()
        self.repo_edit = QLineEdit(str(self.settings.get("feedback_repo", "")))
        self.repo_edit.setPlaceholderText("https://github.com/CHI-sama/bili-asset-box（留空用默认）")
        form3.addRow("反馈仓库", self.repo_edit)
        lay.addLayout(form3)
        repo_tip = QLabel("填 owner/repo 或完整链接，反馈页的 issue 按钮就会指向它；留空用内置默认仓库。")
        repo_tip.setObjectName("Faint")
        repo_tip.setWordWrap(True)
        lay.addWidget(repo_tip)
        self.body.addWidget(frame)

        #限速
        frame2, lay2 = card()
        title = QLabel("限速（对 B 站服务器友好一点）")
        title.setObjectName("SectionTitle")
        lay2.addWidget(title)
        form2 = QFormLayout()

        self.conc = QSpinBox()
        self.conc.setRange(1, 8)
        self.conc.setMaximumWidth(150)
        self.conc.setValue(int(self.settings.get("concurrency", 6)))
        self.conc.valueChanged.connect(lambda v: save_settings({**self.settings, "concurrency": v}))
        form2.addRow("并发下载数", self.conc)

        self.api_ms = QSpinBox()
        self.api_ms.setRange(200, 5000)
        self.api_ms.setMaximumWidth(150)
        self.api_ms.setSingleStep(50)
        self.api_ms.setValue(int(self.settings.get("api_interval_ms", 500)))
        form2.addRow("接口请求间隔 (ms)", self.api_ms)

        self.file_ms = QSpinBox()
        self.file_ms.setRange(0, 5000)
        self.file_ms.setMaximumWidth(150)
        self.file_ms.setSingleStep(10)
        self.file_ms.setValue(int(self.settings.get("file_interval_ms", 150)))
        form2.addRow("图片下载间隔 (ms)", self.file_ms)

        self.kbps = QSpinBox()
        self.kbps.setRange(0, 102400)
        self.kbps.setMaximumWidth(150)
        self.kbps.setSingleStep(64)
        self.kbps.setSuffix(" KB/s")
        self.kbps.setSpecialValueText("不限")
        self.kbps.setValue(int(self.settings.get("per_file_kbps", 0)))
        form2.addRow("单文件限速", self.kbps)
        lay2.addLayout(form2)
        save_row = QHBoxLayout()
        save = primary_button("保存设置")
        save.setMinimumWidth(140)
        save.clicked.connect(self.save)
        save_row.addWidget(save)
        save_row.addStretch(1)
        lay2.addLayout(save_row)
        self.body.addWidget(frame2)
        self.body.addStretch(1)

    #工具
    @staticmethod
    def _wrap(layout: QHBoxLayout) -> "QLabel|QVBoxLayout":
        from PySide6.QtWidgets import QWidget

        w = QWidget()
        w.setLayout(layout)
        return w

    def _pick_dir(self) -> None:
        d = QFileDialog.getExistingDirectory(self, "选择下载目录", self.dir_edit.text() or os.getcwd())
        if d:
            self.dir_edit.setText(d)

    def _pick_ffmpeg(self) -> None:
        f, _ = QFileDialog.getOpenFileName(self, "选择 ffmpeg.exe", "", "可执行文件 (*.exe)")
        if f:
            self.ffmpeg_edit.setText(f)

    def _open_folder(self, path: str) -> None:
        if not path or not os.path.isdir(path):
            self.notify("目录还不存在", "warn")
            return
        try:
            os.startfile(path)  # noqa: S606 - 用户主动打开自己选的目录
        except OSError:
            subprocess.Popen(["explorer", path])

    #保存
    def save(self) -> None:
        new = dict(self.settings)
        new.update(
            {
                "download_dir": self.dir_edit.text().strip(),
                "ffmpeg": self.ffmpeg_edit.text().strip(),
                "feedback_repo": self.repo_edit.text().strip(),
                "concurrency": int(self.conc.value()),
                "api_interval_ms": int(self.api_ms.value()),
                "file_interval_ms": int(self.file_ms.value()),
                "per_file_kbps": int(self.kbps.value()),
            }
        )
        save_settings(new)
        self.settings.update(new)
        try:
            self.core.call(
                "rate_configure",
                {
                    "api_interval_ms": new["api_interval_ms"],
                    "file_interval_ms": new["file_interval_ms"],
                    "per_file_kbps": new["per_file_kbps"],
                },
            )
        except Exception as exc:
            self.notify(f"限速未生效：{exc}", "error")
            return
        self.notify("设置已保存", "ok")
