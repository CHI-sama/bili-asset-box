#!/usr/bin/env python3
"""把 Python 依赖（wheel）直接解压到 venv 的 site-packages。

为什么不用 pip：
    本机沙箱策略会让 pip 在临时目录里创建 .metadata 时返回 WinError 5，
    而 pip 内部没有开关可以绕过。wheel 本质就是 zip，直接解压同样能装出
    可用的包（我们不需要 console_scripts 入口，C++ 侧也只调用绝对路径）。

用法:
    python scripts/install_wheels.py                 # 安装 requirements
    python scripts/install_wheels.py --list          # 只解析将要下载的 wheel
"""
from __future__ import annotations

import argparse
import json
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / ".venv" / "Lib" / "site-packages"
CACHE = ROOT / "third_party" / "_cache" / "wheels"
PY_TAG = f"cp{sys.version_info.major}{sys.version_info.minor}"  #随解释器版本变化，换台机器也能挑到合适的轮子
PLAT = "win_amd64"

#需要安装的包：(PyPI 名称, 版本约束说明)
REQUIREMENTS = [
    ("cmake", None),
    ("ninja", None),
    ("shiboken6", "6.12.0"),
    ("PySide6-Essentials", "6.12.0"),
    ("pillow", None),
    ("qrcode", None),
    #打包成 exe 用（可选）：PyInstaller 及其依赖
    ("pyinstaller", None),
    ("pyinstaller-hooks-contrib", None),
    ("altgraph", None),
    ("pefile", None),
    ("pywin32-ctypes", None),
    ("setuptools", None),
    ("packaging", None),
]


def log(msg: str) -> None:
    print(f"[wheels] {msg}", flush=True)


def fetch_json(url: str):
    req = urllib.request.Request(url, headers={"User-Agent": "BiliAssetBox/0.1"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read().decode("utf-8"))


def pick_wheel(name: str, pinned: str | None) -> tuple[str, str]:
    """从 PyPI JSON 里挑出适合 cp313 / win_amd64 的 wheel。

    返回 (版本, 下载地址)。
    """
    meta = fetch_json(f"https://pypi.org/pypi/{name}/json")
    version = pinned or meta["info"]["version"]
    files = meta["releases"].get(version, [])
    best = None
    for f in files:
        fn = f["filename"]
        if not fn.endswith(".whl"):
            continue
        #例：PySide6_Essentials-6.12.0-cp39-abi3-win_amd64.whl
        parts = fn[:-4].rsplit("-", 3)
        if len(parts) != 4:
            continue
        _, pytag, abitag, plattag = parts
        if plattag not in ("win_amd64", "any"):
            continue
        #解释器兼容性：py3 纯 Python 直接用；本机 cp 标签；cp3x-abi3 走稳定 ABI
        py_ok = pytag in ("py3", "py2.py3", PY_TAG) or (pytag.startswith("cp3") and abitag == "abi3")
        if not py_ok:
            continue
        #打分：原生平台 > 纯 Python；精确版本 > abi3
        score = 0
        if plattag == "win_amd64":
            score += 10
        if pytag == PY_TAG:
            score += 5
        elif pytag.startswith("cp3") and abitag == "abi3":
            score += 3
        if best is None or score > best[0]:
            best = (score, f)
    if best is None:
        raise SystemExit(f"找不到 {name}=={version} 的 {PY_TAG}/{PLAT} wheel")
    return version, best[1]["url"]


def download(url: str, dest: Path) -> Path:
    if dest.exists() and dest.stat().st_size > 0:
        log(f"cache hit {dest.name}")
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(".part")
    log(f"GET {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "BiliAssetBox/0.1"})
    with urllib.request.urlopen(req, timeout=120) as r, tmp.open("wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        while True:
            chunk = r.read(1 << 18)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if total and done % (8 << 20) < (1 << 18):
                log(f"  {done / 1048576:6.1f} / {total / 1048576:.1f} MB")
    tmp.replace(dest)
    log(f"saved {dest.name} ({dest.stat().st_size / 1048576:.1f} MB)")
    return dest


def extract_wheel(whl: Path) -> None:
    """解压 wheel 到 site-packages，处理 .data/purelib 与 .data/platlib。"""
    SITE.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(whl) as zf:
        names = zf.namelist()
        for n in names:
            if "/.data/" in n and ("purelib" in n or "platlib" in n):
                raise SystemExit(f"暂不支持的 wheel 布局: {n}")
        zf.extractall(SITE)
    log(f"installed {whl.name}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    plan = []
    for name, pinned in REQUIREMENTS:
        ver, url = pick_wheel(name, pinned)
        plan.append((name, ver, url))
        log(f"{name}=={ver}")
        if args.list:
            continue
        whl = CACHE / url.rsplit("/", 1)[-1]
        download(url, whl)
        extract_wheel(whl)
    if args.list:
        return 0
    log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
