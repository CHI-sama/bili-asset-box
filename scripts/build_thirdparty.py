# -*- coding: utf-8 -*-
"""
build_thirdparty.py —— 用项目内的 MinGW 工具链静态编译 mbedTLS 与 libcurl。

为什么不用系统里的东西：
  * 本机没有可用 MSVC，系统 MinGW 是 32 位老版本；
  * 沙箱里 Schannel 的 TLS 凭据拿不到（SEC_E_NO_CREDENTIALS），所以 curl 必须用
    mbedTLS 后端，不能用 Windows 自带的 Schannel。

产物（都落在 third_party/ 下，不碰 C 盘其它目录）：
  third_party/lib/libcurl.a  libmbedtls.a  libmbedx509.a  libmbedcrypto.a
  third_party/include/curl/*.h  third_party/include/mbedtls/*.h
用法：
  python scripts/build_thirdparty.py            # 两个都编
  python scripts/build_thirdparty.py --only curl
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
THIRD = ROOT / "third_party"
TOOLCHAIN = THIRD / "toolchain" / "mingw64" / "bin"
CMAKE = ROOT / ".venv" / "Lib" / "site-packages" / "cmake" / "data" / "bin" / "cmake.exe"
MAKE = TOOLCHAIN / "mingw32-make.exe"
GCC = TOOLCHAIN / "gcc.exe"
GXX = TOOLCHAIN / "g++.exe"

MBEDTLS_SRC = THIRD / "src" / "mbedtls-2.28.10"
CURL_SRC = THIRD / "src" / "curl-curl-8_11_1"  #注意 fetch_deps 解压后多一层 curl- 前缀
BUILD = ROOT / "build" / "thirdparty"
MBEDTLS_URL = "https://codeload.github.com/Mbed-TLS/mbedtls/zip/refs/tags/v2.28.10"

#为什么用 2.28 而不是 3.6：
#   3.6 的源码包把 C 框架拆成了 framework 子模块，单独下载的 tar 包缺这个子模块，
#   CMake 配置会直接报错；2.28 是自包含的，curl 8.11 也完全支持。
#为什么用 zip 而不是 tar.gz 解压：
#   Python 的 tarfile 会把目录权限设成 0700，在 Windows 上生成"仅所有者"的受保护
#   DACL，导致当前沙箱进程连列目录都会被拒绝（实测踩过这个坑）。zipfile 不会。


def ensure_mbedtls_source() -> None:
    """源码缺失时自动下载并解压（只写项目内的 third_party/src）。"""
    if MBEDTLS_SRC.exists():
        return
    import io
    import urllib.request
    import zipfile

    print(f"下载 mbedTLS：{MBEDTLS_URL}", flush=True)
    data = urllib.request.urlopen(MBEDTLS_URL, timeout=180).read()
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(THIRD / "src")


def run(cmd: list[str], cwd: Path | None = None) -> None:
    """执行命令，失败时把完整命令行打出来方便排查。"""
    pretty = " ".join(str(c) for c in cmd)
    print(f"\n>>> {pretty}", flush=True)
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if proc.returncode != 0:
        raise SystemExit(f"命令失败（{proc.returncode}）：{pretty}")


def cmake_args(build_dir: Path) -> list[str]:
    return [
        str(CMAKE),
        "-G", "MinGW Makefiles",
        f"-DCMAKE_MAKE_PROGRAM={MAKE}",
        f"-DCMAKE_C_COMPILER={GCC}",
        f"-DCMAKE_CXX_COMPILER={GXX}",
        "-DCMAKE_BUILD_TYPE=Release",
        # mbedTLS 2.28 / curl 的 cmake_minimum_required 低于 3.5，CMake 4 必须显式放宽
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        f"-DCMAKE_INSTALL_PREFIX={THIRD}",
    ]


def prepare_build_dir(build: Path) -> None:
    """源码目录换了（比如 mbedTLS 3.6 -> 2.28）时旧缓存会让 CMake 报错，直接清掉。"""
    if (build / "CMakeCache.txt").exists():
        shutil.rmtree(build, ignore_errors=True)
    build.mkdir(parents=True, exist_ok=True)


def build_mbedtls() -> None:
    build = BUILD / "mbedtls"
    prepare_build_dir(build)
    args = cmake_args(build) + [
        "-DENABLE_PROGRAMS=OFF",
        "-DENABLE_TESTING=OFF",
        "-DUSE_SHARED_MBEDTLS_LIBRARY=OFF",
        "-DUSE_STATIC_MBEDTLS_LIBRARY=ON",
        "-DGEN_FILES=OFF",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        str(MBEDTLS_SRC),
    ]
    run(args, cwd=build)
    run([str(CMAKE), "--build", ".", "--target", "install", "-j", "4"], cwd=build)


def build_curl() -> None:
    build = BUILD / "curl"
    prepare_build_dir(build)
    include = THIRD / "include"
    lib = THIRD / "lib"
    args = cmake_args(build) + [
        "-DBUILD_SHARED_LIBS=OFF",
        "-DBUILD_STATIC_LIBS=ON",
        "-DBUILD_CURL_EXE=OFF",
        "-DENABLE_CURL_MANUAL=OFF",
        #只用 HTTP(S)，其余协议全关，能少一半依赖
        "-DHTTP_ONLY=ON",
        "-DCURL_USE_MBEDTLS=ON",
        "-DCMAKE_USE_SCHANNEL=OFF",
        "-DCMAKE_USE_OPENSSL=OFF",
        f"-DMBEDTLS_INCLUDE_DIR={include}",
        f"-DMBEDTLS_LIBRARY={lib / 'libmbedtls.a'}",
        f"-DMBEDX509_LIBRARY={lib / 'libmbedx509.a'}",
        f"-DMBEDCRYPTO_LIBRARY={lib / 'libmbedcrypto.a'}",
        "-DUSE_LIBIDN2=OFF",
        "-DCURL_USE_LIBPSL=OFF",
        "-DCURL_ZLIB=OFF",
        "-DCURL_BROTLI=OFF",
        "-DCURL_ZSTD=OFF",
        "-DCURL_USE_LIBSSH2=OFF",
        "-DCURL_DISABLE_LDAP=ON",
        "-DCURL_DISABLE_LDAPS=ON",
        "-DCURL_DISABLE_DOH=ON",
        "-DENABLE_WEBSOCKETS=OFF",
        "-DCURL_CA_BUNDLE=none",
        "-DCURL_CA_PATH=none",
        str(CURL_SRC),
    ]
    run(args, cwd=build)
    run([str(CMAKE), "--build", ".", "--target", "install", "-j", "4"], cwd=build)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", choices=["mbedtls", "curl"], help="只编译其中一个")
    args = parser.parse_args()

    if not CMAKE.exists():
        raise SystemExit(f"找不到 cmake：{CMAKE}（先运行 scripts/install_wheels.py）")
    if not MAKE.exists():
        raise SystemExit(f"找不到 mingw32-make：{MAKE}（先运行 scripts/fetch_deps.py）")
    if not CURL_SRC.exists() and args.only != "mbedtls":
        raise SystemExit(f"找不到 curl 源码目录：{CURL_SRC}")

    #把工具链的 dll 目录放进 PATH，CMake 的子进程才能找到 gcc
    os.environ["PATH"] = str(TOOLCHAIN) + os.pathsep + os.environ.get("PATH", "")

    if args.only != "curl":
        ensure_mbedtls_source()
        build_mbedtls()
    if args.only != "mbedtls":
        build_curl()

    print("\n完成：")
    for name in ("libcurl.a", "libmbedtls.a", "libmbedx509.a", "libmbedcrypto.a"):
        p = THIRD / "lib" / name
        print(f"  {'OK ' if p.exists() else '缺失'} {p}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
