#!/usr/bin/env python3
"""Builds the release zip that goes on the GitHub release.

    python tools/make_release.py [--out dist]

Writes LoWRecomp-v<VERSION>-Windows-x64.zip with everything inside one
LoWRecomp-v<VERSION>/ folder and recomplauncher.json at its top, next to
install.bat. The zip holds the installer and sources only - never game files
or built programs (they contain the game's code).
"""
from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Top-level entries that go into the release (relative to the repository).
INCLUDE = [
    "install.bat", "recomplauncher.json", "VERSION", "README.md", "LIESMICH.md",
    "LICENSE", "THIRD_PARTY_NOTICES.md", "docs", "installer", "project", "tools",
]
# Never packed, wherever they turn up.
SKIP_DIRS = {"__pycache__", ".git", "out", "build", "generated_default", "default"}
SKIP_SUFFIXES = {".pyc", ".iso", ".xex", ".pkg", ".pac", ".arc", ".bik", ".bin", ".exe", ".dll", ".pdb"}


def files_to_pack(root: Path):
    for name in INCLUDE:
        p = root / name
        if not p.exists():
            raise SystemExit(f"missing: {name}")
        if p.is_file():
            yield p
            continue
        for f in sorted(p.rglob("*")):
            rel = f.relative_to(root)
            if f.is_dir() or any(part in SKIP_DIRS for part in rel.parts[:-1]):
                continue
            if f.suffix.lower() in SKIP_SUFFIXES:
                continue
            yield f


def check_manifest(root: Path, version: str, allow_old_build: bool) -> None:
    data = json.loads((root / "recomplauncher.json").read_text(encoding="utf-8"))
    prep = data.get("prepare", {})
    # start programs are built by the prepare step; only the prepare step must ship
    for prog in prep.get("program", []):
        if not (root / prog).is_file():
            raise SystemExit(f"recomplauncher.json: prepare program {prog} is missing")
    if prep.get("version") != version and not allow_old_build:
        raise SystemExit(
            f"recomplauncher.json: prepare.version is {prep.get('version')!r}, VERSION is {version!r}.\n"
            "Set prepare.version to the new version so RecompLauncher rebuilds the game for players,\n"
            "or pass --allow-old-build if this release does not change anything that gets built.")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="dist", help="output folder (default: dist)")
    ap.add_argument("--allow-old-build", action="store_true",
                    help="allow prepare.version to differ from VERSION (release without code changes)")
    args = ap.parse_args()

    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    check_manifest(ROOT, version, args.allow_old_build)
    top = f"LoWRecomp-v{version}"
    out_dir = (ROOT / args.out) if not Path(args.out).is_absolute() else Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / f"{top}-Windows-x64.zip"
    count = 0
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in files_to_pack(ROOT):
            z.write(f, f"{top}/{f.relative_to(ROOT).as_posix()}")
            count += 1
    print(f"{zip_path} ({count} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
