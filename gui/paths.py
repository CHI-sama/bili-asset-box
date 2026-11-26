"""paths.py —— 源码运行和打包成 exe 两种形态下的路径规则。

- 源码运行：项目根目录 = gui/ 的上一级，DLL/CA/资源都在项目里。
- PyInstaller 打包后：只读资源在 sys._MEIPASS（BUNDLE_DIR），
  可写的东西（settings.json、data、downloads、logs）放在 exe 旁边（APP_DIR），
  这样用户换台机器解压就能用，也不会往 C 盘别处写。
"""
from __future__ import annotations

import sys
from pathlib import Path

FROZEN = bool(getattr(sys, "frozen", False))

if FROZEN:
    BUNDLE_DIR = Path(getattr(sys, "_MEIPASS", Path(sys.executable).resolve().parent))
    APP_DIR = Path(sys.executable).resolve().parent
else:
    BUNDLE_DIR = Path(__file__).resolve().parents[1]
    APP_DIR = BUNDLE_DIR

#内核 DLL 跟着资源走（打包后在 _MEIPASS 里）
DLL_PATH = BUNDLE_DIR / "build" / "bin" / "bili_core.dll"
#用户配置放在可写目录
SETTINGS_PATH = APP_DIR / "settings.json"
