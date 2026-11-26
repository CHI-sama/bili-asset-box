#!/usr/bin/env python3
"""把 GUI + 内核 DLL 打包成 Windows 桌面程序（PyInstaller，onedir）。

产物：dist/BiliAssetBox/BiliAssetBox.exe（同目录还有 _internal，整个文件夹可拷贝分发）

用法:
    python scripts/build_exe.py            # 打包
    python scripts/build_exe.py --clean    # 先删 dist/ 再打包

说明:
    - 用 onedir 而不是 onefile：启动快，PySide6 解压一次即可，也方便看日志。
    - 只读资源（assets、bili_core.dll、CA 证书）打进 _internal，由 gui/paths.py 定位；
      可写的 settings.json / data / downloads / logs 放在 exe 旁边。
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENV_PY = ROOT / ".venv" / "Scripts" / "python.exe"
DIST = ROOT / "dist"
WORK = ROOT / "build" / "pyi"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true", help="先清掉 dist 和中间目录")
    args = ap.parse_args()

    python = VENV_PY if VENV_PY.exists() else Path(sys.executable)
    dll = ROOT / "build" / "bin" / "bili_core.dll"
    if not dll.exists():
        raise SystemExit(f"找不到内核 DLL：{dll}\n先运行 python scripts/build_core.py")
    if args.clean:
        for d in (DIST, WORK):
            if d.exists():
                shutil.rmtree(d)

    cmd = [
        str(python), "-m", "PyInstaller",
        "--noconfirm", "--clean",
        "--windowed",                      # 不带控制台窗口
        "--name", "BiliAssetBox",
        "--icon", str(ROOT / "assets" / "icon.ico"),
        "--distpath", str(DIST),
        "--workpath", str(WORK),
        "--specpath", str(WORK),
        "--paths", str(ROOT),
        "--add-data", f"{ROOT / 'assets'};assets",
        "--add-data", f"{dll};build/bin",
        "--add-data", f"{ROOT / 'third_party' / 'ca-bundle.crt'};third_party",
        str(ROOT / "run.py"),
    ]
    print("[exe] " + " ".join(cmd), flush=True)
    rc = subprocess.call(cmd, cwd=str(ROOT))
    if rc != 0:
        print(f"[exe] PyInstaller 失败，exit={rc}")
        return rc

    exe = DIST / "BiliAssetBox" / "BiliAssetBox.exe"
    if not exe.exists():
        print("[exe] 没找到产物：", exe)
        return 2
    size = sum(p.stat().st_size for p in (DIST / "BiliAssetBox").rglob("*") if p.is_file())
    print(f"[exe] 完成：{exe}")
    print(f"[exe] 整个目录 {size / 1048576:.0f} MB，可整体拷贝给别人")
    return 0


if __name__ == "__main__":
    sys.exit(main())
