"""theme.py —— 深色主题：颜色常量 + QSS 样式表。

设计目标：
  * 背景偏深灰蓝，卡片略亮，粉色的 B 站主色只用在强调处，避免整屏发光；
  * 圆角统一 10~14px，标题字重比正文高一级；
  * 所有颜色集中在这里，换肤只改这一个文件。
"""
from __future__ import annotations

# B 站粉 + 一组中性色
PINK = "#FB7299"
PINK_DIM = "#D45C7E"
BG = "#0F1115"
BG_ALT = "#141820"
CARD = "#171A21"
CARD_HI = "#1D212B"
BORDER = "#262B36"
TEXT = "#E8EAF0"
TEXT_DIM = "#9AA3B2"
TEXT_FAINT = "#6B7686"
BLUE = "#4C9AFF"
GREEN = "#3DDC97"
YELLOW = "#FFC757"
RED = "#FF6B6B"


def qss() -> str:
    """生成整个应用的样式表。"""
    return f"""
* {{
    font-family: "Microsoft YaHei UI", "Segoe UI", "PingFang SC", sans-serif;
    font-size: 13px;
    color: {TEXT};
}}
QMainWindow, QDialog, #ContentArea {{
    background: {BG};
}}
QWidget#Sidebar {{
    background: {BG_ALT};
    border-right: 1px solid {BORDER};
}}
QLabel#Logo {{
    color: {PINK};
    font-size: 17px;
    font-weight: 700;
    padding: 18px 18px 4px 18px;
}}
QLabel#LogoSub {{
    color: {TEXT_FAINT};
    font-size: 11px;
    padding: 0 18px 14px 18px;
}}
QPushButton#NavButton {{
    background: transparent;
    border: none;
    border-radius: 9px;
    padding: 10px 14px;
    margin: 2px 10px;
    text-align: left;
    color: {TEXT_DIM};
}}
QPushButton#NavButton:hover {{
    background: {CARD};
    color: {TEXT};
}}
QPushButton#NavButton:checked {{
    background: {CARD_HI};
    color: {PINK};
    font-weight: 600;
}}
QPushButton {{
    background: {CARD_HI};
    border: 1px solid {BORDER};
    border-radius: 9px;
    padding: 7px 15px;
}}
QPushButton:hover {{
    border-color: {PINK_DIM};
}}
QPushButton:disabled {{
    color: {TEXT_FAINT};
    background: {CARD};
    border-color: {BORDER};
}}
QPushButton#Primary {{
    background: {PINK};
    border: 1px solid {PINK};
    color: #1A1A1A;
    font-weight: 600;
}}
QPushButton#Primary:hover {{
    background: #FF87A9;
}}
QPushButton#Primary:disabled {{
    background: {CARD};
    border-color: {BORDER};
    color: {TEXT_FAINT};
}}
QPushButton#Ghost {{
    background: transparent;
}}
QLabel#PageTitle {{
    font-size: 21px;
    font-weight: 700;
}}
QLabel#PageDesc {{
    color: {TEXT_DIM};
}}
QLabel#SectionTitle {{
    font-size: 14px;
    font-weight: 600;
    color: {TEXT};
}}
QLabel#Faint {{
    color: {TEXT_FAINT};
    font-size: 12px;
}}
QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTextEdit {{
    background: {CARD};
    border: 1px solid {BORDER};
    border-radius: 9px;
    padding: 7px 10px;
    selection-background-color: {PINK_DIM};
}}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus {{
    border-color: {PINK_DIM};
}}
QComboBox::drop-down {{
    border: none;
    width: 22px;
}}
QComboBox QAbstractItemView {{
    background: {CARD_HI};
    border: 1px solid {BORDER};
    selection-background-color: {CARD_HI};
}}
QProgressBar {{
    background: {CARD};
    border: none;
    border-radius: 6px;
    height: 12px;
    text-align: center;
    color: {TEXT_DIM};
    font-size: 11px;
}}
QProgressBar::chunk {{
    border-radius: 6px;
    background: {PINK};
}}
QListWidget#AssetGrid {{
    background: transparent;
    border: none;
    outline: none;
}}
QListWidget#AssetGrid::item {{
    background: {CARD};
    border: 1px solid {BORDER};
    border-radius: 12px;
    margin: 6px;
    padding: 8px;
    color: {TEXT_DIM};
}}
QListWidget#AssetGrid::item:hover {{
    border-color: {PINK_DIM};
    background: {CARD_HI};
}}
QListWidget#AssetGrid::item:selected {{
    border-color: {PINK};
    background: {CARD_HI};
    color: {TEXT};
}}
QScrollArea, QScrollArea > QWidget > QWidget {{
    background: transparent;
    border: none;
}}
QScrollBar:vertical {{
    background: transparent;
    width: 10px;
    margin: 2px;
}}
QScrollBar::handle:vertical {{
    background: {BORDER};
    border-radius: 5px;
    min-height: 30px;
}}
QScrollBar::handle:vertical:hover {{
    background: #39404E;
}}
QScrollBar::add-line, QScrollBar::sub-line {{
    height: 0;
}}
QTabWidget::pane {{
    border: none;
}}
QTabBar::tab {{
    background: transparent;
    color: {TEXT_DIM};
    padding: 8px 16px;
    margin-right: 4px;
    border-radius: 8px;
}}
QTabBar::tab:selected {{
    background: {CARD_HI};
    color: {PINK};
}}
QTableWidget {{
    background: {CARD};
    border: 1px solid {BORDER};
    border-radius: 10px;
    gridline-color: {BORDER};
    selection-background-color: {CARD_HI};
}}
QHeaderView::section {{
    background: {CARD_HI};
    border: none;
    padding: 7px;
    color: {TEXT_DIM};
}}
QCheckBox, QRadioButton {{
    spacing: 7px;
}}
QCheckBox::indicator, QRadioButton::indicator {{
    width: 15px;
    height: 15px;
    border-radius: 4px;
    border: 1px solid {BORDER};
    background: {CARD};
}}
QCheckBox::indicator:checked {{
    background: {PINK};
    border-color: {PINK};
}}
QToolTip {{
    background: {CARD_HI};
    color: {TEXT};
    border: 1px solid {BORDER};
    padding: 5px;
}}
QFrame#Card {{
    background: {CARD};
    border: 1px solid {BORDER};
    border-radius: 12px;
}}
QFrame#HLine {{
    background: {BORDER};
    max-height: 1px;
    border: none;
}}
QMenu {{
    background: {CARD_HI};
    border: 1px solid {BORDER};
    padding: 5px;
}}
QMenu::item {{
    padding: 6px 22px;
    border-radius: 6px;
}}
QMenu::item:selected {{
    background: {CARD};
    color: {PINK};
}}
"""
