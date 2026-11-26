"""编译 C++ 内核 bili_core.dll。

用法：
    python scripts/build_core.py             # 增量编译
    python scripts/build_core.py --clean     # 清空 build/core 后重编
    python scripts/build_core.py --tests     # 同时编译 core/tests 下的自测程序

依赖：third_party/lib 下的 libcurl / mbedTLS 静态库（先跑 build_thirdparty.py），
以及 third_party/toolchain/mingw64 里的 MinGW 工具链。
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "core"
CMAKE = ROOT / ".venv" / "Lib" / "site-packages" / "cmake" / "data" / "bin" / "cmake.exe"
MAKE = ROOT / "third_party" / "toolchain" / "mingw64" / "bin" / "mingw32-make.exe"
GCC = ROOT / "third_party" / "toolchain" / "mingw64" / "bin" / "gcc.exe"
GXX = ROOT / "third_party" / "toolchain" / "mingw64" / "bin" / "g++.exe"


def run(cmd: list[str], cwd: Path) -> None:
    print("\n>>> " + " ".join(str(c) for c in cmd), flush=True)
    proc = subprocess.run([str(c) for c in cmd], cwd=str(cwd))
    if proc.returncode != 0:
        raise SystemExit(f"命令失败（{proc.returncode}）")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true", help="清空 build/core 再编译")
    ap.add_argument("--tests", action="store_true", help="同时构建 core/tests 里的自测程序")
    args = ap.parse_args()

    for p in (CMAKE, MAKE, GCC, GXX):
        if not p.exists():
            print(f"缺少工具：{p}\n请先运行 scripts/fetch_deps.py 和 scripts/build_thirdparty.py")
            return 1

    if args.clean and BUILD.exists():
        shutil.rmtree(BUILD, ignore_errors=True)
    BUILD.mkdir(parents=True, exist_ok=True)

    cfg = [
        CMAKE,
        "-G", "MinGW Makefiles",
        f"-DCMAKE_MAKE_PROGRAM={MAKE}",
        f"-DCMAKE_C_COMPILER={GCC}",
        f"-DCMAKE_CXX_COMPILER={GXX}",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DBUILD_CORE_TESTS={'ON' if args.tests else 'OFF'}",
        str(ROOT / "core"),
    ]
    run(cfg, cwd=BUILD)
    run([CMAKE, "--build", ".", "-j", "6"], cwd=BUILD)

    dll = ROOT / "build" / "bin" / "bili_core.dll"
    print("\n完成：", dll, "存在" if dll.exists() else "不存在！", sep="")
    if dll.exists():
        print(f"大小：{dll.stat().st_size / 1024 / 1024:.2f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
