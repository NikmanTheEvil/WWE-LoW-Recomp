"""Tests for the asset tools with synthetic data (no game files needed)."""
import os
import struct
import sys
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import kb_hud_build as KB  # noqa: E402
import low_assets as LA  # noqa: E402


def dds_page(fill):
    """256x512 DXT5 page with DDS header, like the button-symbol page."""
    return KB.page_from_bin(bytes([fill]) * LA.PAGE_BYTES)


class GlyphPageTest(unittest.TestCase):
    def test_most_frequent_page_wins(self):
        a, b = dds_page(1), dds_page(2)
        blob = b"junk" + a + b"x" * 13 + b + b + b"tail" + b
        self.assertEqual(LA.glyph_page(blob), b[128:])

    def test_no_page(self):
        with self.assertRaises(SystemExit):
            LA.glyph_page(b"nothing here")


class RecipeTest(unittest.TestCase):
    def test_jobs_roundtrip_and_apply(self):
        # Xbox image: grey background with a blue "button"; PS3 image: same with a pink one
        x = np.full((64, 96, 4), (90, 90, 90, 255), np.uint8)
        p = x.copy()
        yy, xx = np.mgrid[0:64, 0:96]
        disk = (yy - 32) ** 2 + (xx - 30) ** 2 < 12 ** 2
        x[disk] = (40, 90, 230, 255)
        p[disk] = (230, 90, 180, 255)
        xi, pi = Image.fromarray(x, "RGBA"), Image.fromarray(p, "RGBA")
        jobs = KB.find_jobs(xi, pi)
        self.assertEqual(len(jobs), 1)
        self.assertEqual(jobs[0][0], "X")  # blue = X button
        key = (b"GG2D", b"0001", 0, 3, "bpe", 1)
        leaves = KB.recipe_dump({key: ("abc", jobs)})
        back = KB.recipe_load(leaves)
        self.assertIn(key, back)
        sha, jobs2 = back[key]
        self.assertEqual(sha, "abc")
        for j1, j2 in zip(jobs, jobs2):
            self.assertEqual(j1[:5], tuple(j2[:5]))
            self.assertTrue(np.array_equal(j1[5], j2[5]))
        a, used_a = KB.apply_jobs(xi, jobs)
        b, used_b = KB.apply_jobs(xi, jobs2)
        self.assertEqual(used_a, ["X"])
        self.assertEqual(a.tobytes(), b.tobytes())
        self.assertNotEqual(a.tobytes(), xi.tobytes())


class IconTest(unittest.TestCase):
    def test_icon(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            png = os.path.join(d, "i.png")
            Image.new("RGBA", (300, 200), (200, 20, 20, 255)).save(png)
            out = os.path.join(d, "i.ico")

            class A:
                pass
            a = A()
            a.png, a.out = png, out
            LA.cmd_icon(a)
            with Image.open(out) as im:
                self.assertIn((256, 256), im.info["sizes"])


if __name__ == "__main__":
    unittest.main()
