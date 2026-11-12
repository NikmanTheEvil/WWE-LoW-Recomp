#!/usr/bin/env python3
"""LoWRecomp - extracts an Xbox 360 disc image (XDVDFS / "XISO") to a folder.

Works with full disc dumps (XGD2/XGD3, video partition in front of the game
partition) and with trimmed images that start directly with the game partition.

Usage:
  xiso_extract.py list    <image.iso>
  xiso_extract.py extract <image.iso> <output folder>
"""
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Start of the game partition for the known disc layouts
PARTITION_OFFSETS = [0, 0xFD90000, 0x2080000, 0x18300000]
ATTR_DIRECTORY = 0x10


class XisoError(Exception):
    pass


class Xiso:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.size = os.fstat(self.f.fileno()).st_size
        self.base = None
        for off in PARTITION_OFFSETS:
            if off + 32 * SECTOR + 0x800 > self.size:
                continue
            self.f.seek(off + 32 * SECTOR)
            if self.f.read(len(MAGIC)) == MAGIC:
                self.base = off
                break
        if self.base is None:
            self.f.close()
            raise XisoError("Not an Xbox 360 disc image (XDVDFS volume descriptor not found)")
        self.f.seek(self.base + 32 * SECTOR + 20)
        self.root_sector, self.root_size = struct.unpack("<II", self.f.read(8))

    def close(self):
        self.f.close()

    def _read(self, sector, size, offset=0):
        self.f.seek(self.base + sector * SECTOR + offset)
        return self.f.read(size)

    def entries(self, sector, size):
        """All entries of one directory table: (name, is_dir, start sector, size)."""
        if size == 0:
            return []
        table = self._read(sector, size)
        out = []
        stack = [0]
        seen = set()
        while stack:
            off = stack.pop()
            if off in seen or off + 14 > len(table):
                continue
            seen.add(off)
            left, right, start, length, attr, nlen = struct.unpack_from("<HHIIBB", table, off)
            if left == 0xFFFF and right == 0xFFFF and start == 0xFFFFFFFF:
                continue  # padding
            name = table[off + 14:off + 14 + nlen].decode("latin-1")
            out.append((name, bool(attr & ATTR_DIRECTORY), start, length))
            if left not in (0, 0xFFFF):
                stack.append(left * 4)
            if right not in (0, 0xFFFF):
                stack.append(right * 4)
        return sorted(out, key=lambda e: e[0].lower())

    def walk(self, sector=None, size=None, prefix=""):
        """Yields (relative path, is_dir, start sector, size) for every entry."""
        if sector is None:
            sector, size = self.root_sector, self.root_size
        for name, is_dir, start, length in self.entries(sector, size):
            if name in ("", ".", "..") or "/" in name or "\\" in name:
                continue  # never write outside the target folder
            rel = prefix + name
            yield rel, is_dir, start, length
            if is_dir:
                yield from self.walk(start, length, rel + "/")

    def extract_file(self, start, length, dst, progress=None):
        self.f.seek(self.base + start * SECTOR)
        remaining = length
        with open(dst, "wb") as o:
            while remaining:
                chunk = self.f.read(min(remaining, 8 << 20))
                if not chunk:
                    raise XisoError(f"Image truncated while reading {dst}")
                o.write(chunk)
                remaining -= len(chunk)
                if progress:
                    progress(len(chunk))


def extract(iso_path, out_dir, log=print):
    x = Xiso(iso_path)
    try:
        items = list(x.walk())
        total = sum(l for _, d, _, l in items if not d)
        done = [0, 0]
        log(f"Image: {len(items)} entries, {total / (1 << 30):.2f} GB, game partition at 0x{x.base:X}")

        def progress(n):
            done[0] += n
            pct = int(done[0] * 100 / max(1, total))
            if pct >= done[1] + 5:
                done[1] = pct
                log(f"  {pct}%")

        for rel, is_dir, start, length in items:
            dst = os.path.join(out_dir, *rel.split("/"))
            if is_dir:
                os.makedirs(dst, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if os.path.isfile(dst) and os.path.getsize(dst) == length:
                progress(length)
                continue
            x.extract_file(start, length, dst + ".part", progress)
            os.replace(dst + ".part", dst)
        log("Extraction finished.")
    finally:
        x.close()


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "list":
        x = Xiso(sys.argv[2])
        for rel, is_dir, start, length in x.walk():
            print(f"{'<DIR>' if is_dir else length:>12}  {rel}")
        x.close()
        return 0
    if len(sys.argv) == 4 and sys.argv[1] == "extract":
        try:
            extract(sys.argv[2], sys.argv[3], lambda m: print(m, flush=True))
        except XisoError as e:
            print(f"ERROR: {e}", file=sys.stderr)
            return 1
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
