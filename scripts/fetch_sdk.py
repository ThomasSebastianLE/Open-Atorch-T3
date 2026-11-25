#!/usr/bin/env python3
"""Idempotent chunked downloader for JieLi e_badge_707_sdk_200 from GitLab."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.parse
import urllib.request
from pathlib import Path

BASE_URL = "https://gitlab.zh-jieli.com/e_badge/e_badge_707_sdk_200"
API_TREE = "https://gitlab.zh-jieli.com/api/v4/projects/e_badge%2Fe_badge_707_sdk_200/repository/tree"
REF = "main"

ROOT_RAW_FILES = ["README.md", ".gitignore", "project.jlproj"]
DIRECT_SUBTREES = [
    "SDK/.vscode",
    "SDK/audio",
    "SDK/build",
    "SDK/interface",
    "SDK/tools",
    "SDK/cpu/components",
    "SDK/cpu/config",
    "SDK/cpu/periph_demo",
    "SDK/cpu/power",
    "SDK/cpu/br35/charge",
    "SDK/cpu/br35/liba",
    "SDK/cpu/br35/periph",
    "SDK/cpu/br35/power",
    "SDK/cpu/br35/ui_driver",
]
SPLIT_DIRS = ["SDK", "SDK/apps", "SDK/cpu/br35", "SDK/cpu/br35/tools"]
SKIP_SUBTREES = {"SDK/cpu/br35/tools/UI工程"}


def safe_name(path: str) -> str:
    return path.replace("/", "__").replace(".", "_")


def list_tree(repo_path: str) -> list[dict]:
    items = []
    page = 1
    while True:
        q = urllib.parse.urlencode({"per_page": 100, "page": page, "path": repo_path})
        req = urllib.request.Request(f"{API_TREE}?{q}", headers={"User-Agent": "curl/8.0"})
        with urllib.request.urlopen(req, timeout=30) as resp:
            batch = json.loads(resp.read().decode("utf-8"))
        if not batch:
            break
        items.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    return items


def curl_download(url: str, dst: Path) -> None:
    part = dst.with_name(dst.name + ".part")
    if part.exists():
        part.unlink()
    cmd = [
        "curl.exe",
        "-fL",
        "--http1.1",
        "--retry",
        "3",
        "--retry-delay",
        "3",
        "--connect-timeout",
        "20",
        "-sS",
        "-o",
        str(part),
        url,
    ]
    res = subprocess.run(cmd)
    if res.returncode != 0 or not part.exists():
        if part.exists():
            part.unlink()
        raise RuntimeError(f"curl failed (rc={res.returncode}) for {url}")
    part.replace(dst)


def fetch_raw_file(repo_path: str, sdk_dir: Path, done_dir: Path, force: bool) -> None:
    marker = done_dir / f"raw_{safe_name(repo_path)}.done"
    dst = sdk_dir / repo_path
    if marker.exists() and dst.exists() and not force:
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    enc = "/".join(urllib.parse.quote(p, safe="") for p in repo_path.split("/"))
    url = f"{BASE_URL}/-/raw/{REF}/{enc}"
    curl_download(url, dst)
    marker.write_text("ok\n", encoding="utf-8")


def fetch_subtree(subpath: str, sdk_dir: Path, cache_dir: Path, done_dir: Path, force: bool) -> None:
    marker = done_dir / f"tree_{safe_name(subpath)}.done"
    if marker.exists() and not force:
        print(f"[setup-sdk] skip {subpath} (cached)")
        return

    tar_path = cache_dir / f"{safe_name(subpath)}.tar.gz"
    q = urllib.parse.urlencode({"path": subpath})
    url = f"{BASE_URL}/-/archive/{REF}/e_badge_707_sdk_200-{REF}.tar.gz?{q}"

    for attempt in range(1, 3):
        if force or not tar_path.exists():
            print(f"[setup-sdk] downloading {subpath} (attempt {attempt})...")
            curl_download(url, tar_path)

        try:
            with tarfile.open(tar_path, "r:gz") as tf:
                members = tf.getmembers()
                for m in members:
                    if m.islnk() or m.issym():
                        continue
                    parts = m.name.split("/", 1)
                    if len(parts) < 2 or not parts[1]:
                        continue
                    rel = Path(parts[1])
                    if rel.is_absolute() or ".." in rel.parts:
                        raise RuntimeError(f"Unsafe archive path: {m.name}")
                    target = sdk_dir / rel
                    if m.isdir():
                        target.mkdir(parents=True, exist_ok=True)
                    elif m.isfile():
                        target.parent.mkdir(parents=True, exist_ok=True)
                        src_f = tf.extractfile(m)
                        if src_f is None:
                            continue
                        with src_f, open(target, "wb") as out_f:
                            shutil.copyfileobj(src_f, out_f)
            break
        except (tarfile.TarError, EOFError, OSError) as exc:
            print(f"[setup-sdk] archive verify/extract failed for {subpath}: {exc}")
            if tar_path.exists():
                tar_path.unlink()
            if attempt == 2:
                raise
    marker.write_text("ok\n", encoding="utf-8")
    print(f"[setup-sdk] ok {subpath}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    sdk_dir = repo_root / "sdk" / "ac707n"
    cache_dir = repo_root / ".cache" / "sdk-chunks"
    done_dir = cache_dir / "done"
    complete_flag = sdk_dir / ".sdk-complete"
    makefile = sdk_dir / "SDK" / "Makefile"

    if complete_flag.exists() and makefile.exists() and not args.force:
        print(f"[setup-sdk] AC707N SDK already ready at {sdk_dir}")
        return 0

    if args.force and sdk_dir.exists():
        shutil.rmtree(sdk_dir)
    if args.force and done_dir.exists():
        shutil.rmtree(done_dir)

    sdk_dir.mkdir(parents=True, exist_ok=True)
    cache_dir.mkdir(parents=True, exist_ok=True)
    done_dir.mkdir(parents=True, exist_ok=True)

    for f in ROOT_RAW_FILES:
        fetch_raw_file(f, sdk_dir, done_dir, args.force)

    subtrees = list(DIRECT_SUBTREES)
    for parent in SPLIT_DIRS:
        for entry in list_tree(parent):
            if entry["type"] == "blob":
                fetch_raw_file(entry["path"], sdk_dir, done_dir, args.force)
            elif entry["type"] == "tree" and entry["path"] not in subtrees and entry["path"] not in SPLIT_DIRS and entry["path"] != "SDK/cpu":
                # Only split subdirs inside SPLIT_DIRS that are not parent dirs of DIRECT_SUBTREES
                if entry["path"] in SKIP_SUBTREES:
                    print(f"[setup-sdk] skip optional non-build dir: {entry['path']}")
                elif parent in ("SDK/apps", "SDK/cpu/br35/tools"):
                    subtrees.append(entry["path"])

    for st in subtrees:
        fetch_subtree(st, sdk_dir, cache_dir, done_dir, args.force)

    required = [
        sdk_dir / "SDK" / "Makefile",
        sdk_dir / "SDK" / "cpu" / "br35" / "sdk_ld.c",
        sdk_dir / "SDK" / "apps",
    ]
    missing = [str(p) for p in required if not p.exists()]
    if missing:
        raise RuntimeError(f"SDK incomplete, missing: {missing}")

    complete_flag.write_text("e_badge_707_sdk_200 main\n", encoding="utf-8")
    print(f"[setup-sdk] Done: {makefile}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
