#!/usr/bin/env python3
"""Fetch the SDL2 source tree used by the Android build.

The Android port builds SDL2 from source (the desktop build links the
prebuilt MinGW package in third_party/sdl2_dl).  This script mirrors that
download-and-unpack pattern: it pulls the official 2.30.11 release zip and
unpacks it into third_party/SDL2, which is gitignored.

Usage:
    python tools/fetch_sdl2_source.py [--force]
"""

import argparse
import os
import shutil
import sys
import tempfile
import urllib.request
import zipfile

SDL_VERSION = "2.30.11"
SDL_URL = (
    "https://github.com/libsdl-org/SDL/releases/download/"
    f"release-{SDL_VERSION}/SDL2-{SDL_VERSION}.zip"
)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "third_party", "SDL2")


def already_present() -> bool:
    return (
        os.path.isfile(os.path.join(DEST, "CMakeLists.txt"))
        and os.path.isdir(os.path.join(DEST, "android-project"))
    )


def download(url: str, out_path: str) -> None:
    print(f"Downloading {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "moonwalker-fetch-sdl2"})

    def progress(blocks: int, block_size: int, total: int) -> None:
        if total <= 0:
            return
        done = min(blocks * block_size, total)
        pct = 100.0 * done / total
        sys.stdout.write(f"\r  {done / (1024 * 1024):6.1f} / {total / (1024 * 1024):.1f} MiB ({pct:5.1f}%)")
        sys.stdout.flush()

    with urllib.request.urlopen(req) as resp, open(out_path, "wb") as f:
        total = int(resp.headers.get("Content-Length") or 0)
        block = 256 * 1024
        done = 0
        while True:
            chunk = resp.read(block)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            progress(done // block, block, total)
    print()


def unpack(zip_path: str) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        print("Extracting...")
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(tmp)
        # The archive contains a single SDL2-<version>/ top-level directory.
        entries = [e for e in os.listdir(tmp) if os.path.isdir(os.path.join(tmp, e))]
        if len(entries) != 1:
            raise RuntimeError(f"unexpected archive layout: {entries}")
        src = os.path.join(tmp, entries[0])
        if os.path.isdir(DEST):
            shutil.rmtree(DEST)
        shutil.copytree(src, DEST)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="re-download even if present")
    args = parser.parse_args()

    if already_present() and not args.force:
        print(f"SDL2 {SDL_VERSION} source already present at {DEST}")
        print("Use --force to re-download.")
        return 0

    os.makedirs(os.path.dirname(DEST), exist_ok=True)
    fd, tmp_zip = tempfile.mkstemp(suffix=".zip", prefix="SDL2-")
    os.close(fd)
    try:
        download(SDL_URL, tmp_zip)
        unpack(tmp_zip)
    finally:
        if os.path.exists(tmp_zip):
            os.remove(tmp_zip)

    if not already_present():
        print("ERROR: extraction did not produce the expected SDL2 tree.", file=sys.stderr)
        return 1
    print(f"SDL2 {SDL_VERSION} source ready at {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
