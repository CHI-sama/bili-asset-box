"""run.py —— 双击运行的入口。

Windows 下用 `python run.py` 或打包后的 exe 启动；如果报找不到 PySide6，
请用项目自带的虚拟环境：`.venv\\Scripts\\python.exe run.py`。
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def main() -> int:
    #保证 `python run.py` 在任意工作目录下都能 import 到 gui 包
    if str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))
    from gui.app import main as app_main

    return app_main()


if __name__ == "__main__":
    raise SystemExit(main())
