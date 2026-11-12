"""Tests for tools/xiso_extract.py with synthetic XDVDFS images."""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import xiso_extract as X  # noqa: E402

SECTOR = 2048


def build_image(path, tree, partition_offset=0):
    """tree: {name: bytes | dict}. Writes a minimal XDVDFS volume (binary tree per directory)."""
    data_chunks = []  # (sector, bytes)
    next_sector = [40]

    def alloc(nbytes):
        s = next_sector[0]
        next_sector[0] += max(1, (nbytes + SECTOR - 1) // SECTOR)
        return s

    def write_dir(d):
        names = sorted(d, key=str.lower)
        info = {}
        for n in names:
            v = d[n]
            if isinstance(v, dict):
                info[n] = (True,) + write_dir(v)
            else:
                s = alloc(len(v))
                data_chunks.append((s, v))
                info[n] = (False, s, len(v))
        # balanced binary tree over the sorted names
        entries = []

        def place(lo, hi):
            if lo >= hi:
                return None
            mid = (lo + hi) // 2
            idx = len(entries)
            entries.append([names[mid], None, None])
            entries[idx][1] = place(lo, mid)
            entries[idx][2] = place(mid + 1, hi)
            return idx

        place(0, len(names))
        offs, pos = [], 0
        for name, _, _ in entries:
            offs.append(pos)
            pos += (14 + len(name) + 3) & ~3
        table = bytearray()
        for i, (name, l, r) in enumerate(entries):
            is_dir, s, n = info[name]
            lo = offs[l] // 4 if l is not None else 0
            ro = offs[r] // 4 if r is not None else 0
            e = struct.pack("<HHIIBB", lo, ro, s, n, 0x10 if is_dir else 0x80, len(name)) + name.encode()
            e += b"\xff" * (((len(e) + 3) & ~3) - len(e))
            table += e
        size = len(table)
        sec = alloc(size)
        data_chunks.append((sec, bytes(table)))
        return sec, size

    root_sec, root_size = write_dir(tree)
    with open(path, "wb") as f:
        vd = bytearray(SECTOR)
        vd[0:20] = X.MAGIC
        struct.pack_into("<II", vd, 20, root_sec, root_size)
        vd[0x7EC:0x7EC + 20] = X.MAGIC
        f.seek(partition_offset + 32 * SECTOR)
        f.write(vd)
        for s, b in data_chunks:
            f.seek(partition_offset + s * SECTOR)
            f.write(b)
        f.truncate(partition_offset + next_sector[0] * SECTOR)


TREE = {
    "default.xex": b"XEX2" + os.urandom(5000),
    "nxeart": os.urandom(100),
    "pac": {
        "game2dHD.pac": os.urandom(70000),
        "menu": {"fontHD.pac": os.urandom(3000), "LoadHD.pac": b""},
        "evt": {},
    },
    "movies": {"logo.bik": os.urandom(10)},
    "Zeta.arc": b"z",
}


def flatten(tree, prefix=""):
    out = {}
    for k, v in tree.items():
        if isinstance(v, dict):
            out.update(flatten(v, prefix + k + "/"))
        else:
            out[prefix + k] = v
    return out


class XisoTest(unittest.TestCase):
    def roundtrip(self, offset):
        with tempfile.TemporaryDirectory() as d:
            iso = os.path.join(d, "game.iso")
            build_image(iso, TREE, offset)
            out = os.path.join(d, "out")
            X.extract(iso, out, log=lambda m: None)
            for rel, data in flatten(TREE).items():
                with open(os.path.join(out, *rel.split("/")), "rb") as f:
                    self.assertEqual(f.read(), data, rel)
            self.assertTrue(os.path.isdir(os.path.join(out, "pac", "evt")))
            # second run skips existing files and still succeeds
            X.extract(iso, out, log=lambda m: None)

    def test_trimmed_image(self):
        self.roundtrip(0)

    def test_full_xgd2_dump(self):
        self.roundtrip(0xFD90000)

    def test_not_an_image(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.iso")
            with open(p, "wb") as f:
                f.write(os.urandom(200000))
            with self.assertRaises(X.XisoError):
                X.Xiso(p)


if __name__ == "__main__":
    unittest.main()
