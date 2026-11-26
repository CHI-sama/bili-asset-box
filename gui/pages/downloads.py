"""downloads.py —— 任务页：显示所有下载任务的进度和最近事件。

数据全部来自 JobTracker 的信号，页面自己不轮询，也不碰网络。
"""
from __future__ import annotations

import time

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QProgressBar,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from ..widgets.common import card, hline, primary_button
from .base import Page


class DownloadsPage(Page):
    title = "任务"
    desc = "下载中的文件、失败原因和完成情况都会实时显示在这里。"

    def __init__(self, win, parent=None):
        super().__init__(win, parent)
        self.rows: dict[int, dict] = {}

        top = QWidget()
        tl = QHBoxLayout(top)
        tl.setContentsMargins(0, 0, 0, 0)
        tl.setSpacing(12)
        self.summary = QLabel("暂无任务")
        self.summary.setObjectName("PageDesc")
        tl.addWidget(self.summary, 1)
        self.cancel_btn = primary_button("取消全部")
        self.cancel_btn.clicked.connect(self._cancel_all)
        self.cancel_btn.setEnabled(False)
        tl.addWidget(self.cancel_btn)
        self.body.addWidget(top)

        frame, fl = card(padding=12)
        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["任务 / 文件", "状态", "进度", "说明"])
        self.table.verticalHeader().setVisible(False)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setShowGrid(False)
        hh = self.table.horizontalHeader()
        hh.setDefaultAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        hh.setStretchLastSection(False)
        hh.setSectionResizeMode(0, QHeaderView.Stretch)
        hh.setSectionResizeMode(1, QHeaderView.Fixed)
        hh.setSectionResizeMode(2, QHeaderView.Fixed)
        hh.setSectionResizeMode(3, QHeaderView.Fixed)
        self.table.setColumnWidth(1, 90)
        self.table.setColumnWidth(2, 190)
        self.table.setColumnWidth(3, 330)
        for i in range(4):
            it = self.table.horizontalHeaderItem(i)
            if it is not None:
                it.setTextAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        fl.addWidget(self.table)
        self.body.addWidget(frame, 1)

        self.empty = QLabel("任务开始后会出现在这里。")
        self.empty.setObjectName("Faint")
        self.empty.setAlignment(Qt.AlignCenter)
        self.body.addWidget(self.empty)

        self.tracker.job_event.connect(self._on_event)
        self.tracker.job_progress.connect(self._on_progress)
        self.tracker.job_finished.connect(self._on_finished)

    #外部入口
    def attach(self, job_id: int, title: str, total: int) -> None:
        row = self.table.rowCount()
        self.table.insertRow(row)
        self.rows[job_id] = {"row": row, "title": title, "total": total, "done": 0, "failed": 0, "started": time.time()}
        self.table.setItem(row, 0, QTableWidgetItem(title))
        self.table.setItem(row, 1, QTableWidgetItem("等待中"))
        bar = QProgressBar()
        bar.setRange(0, max(1, total))
        bar.setValue(0)
        bar.setFormat("%v / %m")
        self.table.setCellWidget(row, 2, bar)
        self.table.setItem(row, 3, QTableWidgetItem(f"{total} 个文件"))
        self.empty.hide()
        self.cancel_btn.setEnabled(True)
        self._refresh_summary()

    #信号处理
    def _on_event(self, ev: dict) -> None:
        jid = ev.get("job_id")
        info = self.rows.get(jid)
        if not info:
            return
        row = info["row"]
        kind = ev.get("type")
        bar = self.table.cellWidget(row, 2)
        if kind == "file_done":
            info["done"] += 1
            if isinstance(bar, QProgressBar):
                bar.setValue(info["done"])
            self.table.setItem(row, 1, QTableWidgetItem("下载中"))
            self.table.setItem(row, 3, QTableWidgetItem(ev.get("name", "")))
        elif kind == "file_skip":
            info["done"] += 1
            if isinstance(bar, QProgressBar):
                bar.setValue(info["done"])
            self.table.setItem(row, 3, QTableWidgetItem("已存在，跳过"))
        elif kind == "file_error":
            info["failed"] += 1
            if isinstance(bar, QProgressBar):
                bar.setValue(info["done"] + info["failed"])
            self.table.setItem(row, 1, QTableWidgetItem("出错"))
            self.table.setItem(row, 3, QTableWidgetItem(str(ev.get("message", "未知错误"))))
        elif kind == "progress":
            #单文件进度：显示在"说明"列，不动总进度条
            if ev.get("total"):
                pct = 100.0 * float(ev.get("done", 0)) / float(ev["total"])
                self.table.setItem(row, 3, QTableWidgetItem(f"{pct:5.1f}%  {_human(ev.get('done', 0))}"))
        elif kind == "file_start":
            self.table.setItem(row, 1, QTableWidgetItem("下载中"))
            self.table.setItem(row, 3, QTableWidgetItem(str(ev.get("name", ""))))
        elif kind == "fatal":
            self.table.setItem(row, 1, QTableWidgetItem("失败"))
            self.table.setItem(row, 3, QTableWidgetItem(str(ev.get("message", ""))))

    def _on_progress(self, p: dict) -> None:
        info = self.rows.get(p.get("job_id"))
        if not info:
            return
        info["done"] = p.get("done", info["done"])
        info["failed"] = p.get("failed", info["failed"])
        state = p.get("state", "")
        label = {"running": "下载中", "done": "完成", "failed": "失败", "cancelled": "已取消"}.get(state, state)
        self.table.setItem(info["row"], 1, QTableWidgetItem(label))
        self._refresh_summary()

    def _on_finished(self, p: dict) -> None:
        info = self.rows.pop(p.get("job_id"), None)
        if not info:
            return
        row = info["row"]
        state = p.get("state", "done")
        text = {
            "done": f"完成 · 成功 {p.get('done',0)} · 跳过 {p.get('skipped',0)} · 失败 {p.get('failed',0)}",
            "cancelled": "已取消",
            "failed": f"失败：{p.get('message', '见日志')}",
        }.get(state, state)
        self.table.setItem(row, 1, QTableWidgetItem("完成" if state == "done" else ("已取消" if state == "cancelled" else "失败")))
        self.table.setItem(row, 3, QTableWidgetItem(text))
        bar = self.table.cellWidget(row, 2)
        if isinstance(bar, QProgressBar) and state == "done":
            bar.setValue(bar.maximum())
        self.cancel_btn.setEnabled(bool(self.rows))
        self._refresh_summary()
        if state == "done":
            self.win.notify(f"{info['title']}：{text}", "ok")
        elif state == "failed":
            self.win.notify(f"{info['title']}：{text}", "error")

    #汇总
    def _refresh_summary(self) -> None:
        if not self.rows:
            self.summary.setText("暂无任务")
            return
        total = sum(r["total"] for r in self.rows.values())
        done = sum(r["done"] for r in self.rows.values())
        failed = sum(r["failed"] for r in self.rows.values())
        self.summary.setText(f"{len(self.rows)} 个任务进行中 · 已完成 {done}/{total} 个文件 · 失败 {failed}")
        self.cancel_btn.setEnabled(True)

    def _cancel_all(self) -> None:
        for jid in list(self.rows):
            self.tracker.cancel(jid)
        self.win.notify("已请求取消，正在收尾…", "warn")


def _human(n: float) -> str:
    """字节数转人话。"""
    for unit in ("B", "KB", "MB", "GB"):
        if abs(n) < 1024:
            return f"{n:.0f}{unit}" if unit == "B" else f"{n:.1f}{unit}"
        n /= 1024
    return f"{n:.1f}TB"
