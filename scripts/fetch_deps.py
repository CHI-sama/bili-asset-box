#!/usr/bin/env python3
"""下载并解压 BiliAssetBox 的第三方依赖。

只用 Python 的网络栈（urllib / OpenSSL）。在本机普通 curl.exe / .NET 走 Schannel
会报 SEC_E_NO_CREDENTIALS，但 Python 的 OpenSSL 正常可用，因此所有联网下载都放在
这个脚本里。

用法:
    python scripts/fetch_deps.py            # 缺什么下什么
    python scripts/fetch_deps.py --force    # 全部重下

产物（全部位于项目内 third_party/）:
    toolchain/mingw64/...        WinLibs MinGW-w64 GCC (x86_64, UCRT)
    src/curl-8.11.1/             libcurl 源码
    src/mbedtls-3.6.2/           mbedTLS 源码（libcurl 的 TLS 后端）
    include/nlohmann/json.hpp    JSON 单头文件
    cacert.pem                   CA 根证书
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import tarfile
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TP = ROOT / "third_party"

WINLIBS_VER = "16.2.0posix-23.1.3-14.0.0-ucrt-r3"
WINLIBS_NAME = f"winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3"
WINLIBS_URL = (
    "https://github.com/brechtsanders/winlibs_mingw/releases/download/"
    f"{WINLIBS_VER}/{WINLIBS_NAME}.zip"
)
WINLIBS_SHA_URL = WINLIBS_URL + ".sha256"

CURL_VER = "8.11.1"
CURL_URL = f"https://codeload.github.com/curl/curl/tar.gz/refs/tags/curl-{CURL_VER.replace('.', '_')}"
MBEDTLS_VER = "3.6.2"
MBEDTLS_URL = f"https://codeload.github.com/Mbed-TLS/mbedtls/tar.gz/refs/tags/v{MBEDTLS_VER}"
JSON_URL = "https://cdn.jsdelivr.net/gh/nlohmann/json@v3.11.3/single_include/nlohmann/json.hpp"
CACERT_URL = "https://curl.se/ca/cacert.pem"

UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}


def log(msg: str) -> None:
    print(f"[deps] {msg}", flush=True)


def download(url: str, dest: Path, force: bool = False) -> Path:
    if dest.exists() and dest.stat().st_size > 0 and not force:
        log(f"skip (exists) {dest.relative_to(ROOT)}")
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    log(f"GET {url}")
    req = urllib.request.Request(url, headers=UA)
    started = time.time()
    with urllib.request.urlopen(req, timeout=60) as r, tmp.open("wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        last = -1
        while True:
            chunk = r.read(1024 * 256)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if total:
                pct = int(done * 100 / total)
                if pct >= last + 5:
                    last = pct
                    mb = done / 1048576
                    log(f"  {pct:3d}%  {mb:7.1f} / {total / 1048576:.1f} MB")
    tmp.replace(dest)
    log(f"saved {dest.relative_to(ROOT)} ({dest.stat().st_size / 1048576:.1f} MB, {time.time() - started:.0f}s)")
    return dest


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_sha256(path: Path, expected: str) -> bool:
    actual = sha256_file(path)
    ok = actual.lower() == expected.lower()
    log(f"sha256 {'OK ' if ok else 'MISMATCH'} {path.name}")
    if not ok:
        log(f"  expected {expected}")
        log(f"  actual   {actual}")
    return ok


def extract_zip(zpath: Path, dest: Path) -> None:
    log(f"extract {zpath.name} -> {dest.relative_to(ROOT)}")
    dest.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zpath) as zf:
        zf.extractall(dest)


def extract_tar(tpath: Path, dest: Path) -> None:
    log(f"extract {tpath.name} -> {dest.relative_to(ROOT)}")
    dest.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tpath) as tf:
        #防止路径穿越
        for member in tf.getmembers():
            target = (dest / member.name).resolve()
            if not str(target).startswith(str(dest.resolve())):
                raise RuntimeError(f"unsafe tar member: {member.name}")
        tf.extractall(dest)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="重新下载全部依赖")
    ap.add_argument("--skip-toolchain", action="store_true", help="跳过 276MB 的 MinGW 工具链")
    args = ap.parse_args()

    TP.mkdir(parents=True, exist_ok=True)
    cache = TP / "_cache"
    cache.mkdir(exist_ok=True)

    ok = True

    # 1) MinGW 工具链
    tc_exe = TP / "toolchain" / "mingw64" / "bin" / "g++.exe"
    if not tc_exe.exists() and not args.skip_toolchain:
        zpath = download(WINLIBS_URL, cache / f"{WINLIBS_NAME}.zip", args.force)
        try:
            sha_txt = download(WINLIBS_SHA_URL, cache / f"{WINLIBS_NAME}.zip.sha256", args.force)
            expected = sha_txt.read_text(encoding="utf-8", errors="replace").split()[0]
            ok &= verify_sha256(zpath, expected)
        except Exception as e:  #校验不是致命错误
            log(f"WARN sha256 获取失败: {e}")
        extract_zip(zpath, TP / "toolchain")
        #解压后是 mingw64/ 目录；若名字不同则找 g++.exe
        if not tc_exe.exists():
            found = list((TP / "toolchain").rglob("bin/g++.exe"))
            if found:
                src = found[0].parent.parent
                if src != TP / "toolchain" / "mingw64":
                    if (TP / "toolchain" / "mingw64").exists():
                        shutil.rmtree(TP / "toolchain" / "mingw64")
                    src.rename(TP / "toolchain" / "mingw64")
    log(f"toolchain: {'OK' if tc_exe.exists() else 'MISSING'} {tc_exe}")

    # 2) curl / mbedtls 源码
    curl_dir = TP / "src" / f"curl-{CURL_VER}"
    if not (curl_dir / "lib" / "easy.c").exists():
        tgz = download(CURL_URL, cache / f"curl-{CURL_VER}.tar.gz", args.force)
        extract_tar(tgz, TP / "src")
        got = list((TP / "src").glob("curl-*"))
        log(f"curl src: {[p.name for p in got]}")
    mbed_dir = TP / "src" / f"mbedtls-{MBEDTLS_VER}"
    if not (mbed_dir / "include" / "mbedtls" / "ssl.h").exists():
        tgz = download(MBEDTLS_URL, cache / f"mbedtls-{MBEDTLS_VER}.tar.gz", args.force)
        extract_tar(tgz, TP / "src")
        got = list((TP / "src").glob("mbedtls-*"))
        log(f"mbedtls src: {[p.name for p in got]}")

    # 3) nlohmann/json 单头
    jpath = TP / "include" / "nlohmann" / "json.hpp"
    if not jpath.exists() or args.force:
        download(JSON_URL, jpath, args.force)

    # 4) CA 根证书
    capath = TP / "cacert.pem"
    if not capath.exists() or args.force:
        download(CACERT_URL, capath, args.force)

    (TP / "VERSIONS.txt").write_text(
        "WinLibs mingw-w64 GCC 16.2.0 (x86_64, UCRT, posix, SEH)\n"
        f"curl {CURL_VER}\n"
        f"mbedTLS {MBEDTLS_VER}\n"
        "nlohmann/json 3.11.3 (single header)\n"
        "cacert.pem from curl.se\n"
        "PySide6 6.12.0 (venv)\n",
        encoding="utf-8",
    )
    log("done")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
