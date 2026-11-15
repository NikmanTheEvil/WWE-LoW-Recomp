"""Checks recomplauncher.json against the project (no game files needed)."""
import json
import re
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class RecompLauncherManifest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads((ROOT / "recomplauncher.json").read_text(encoding="utf-8"))
        cls.version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        cls.toml = (ROOT / "project" / "config" / "lowrecomp.toml").read_text(encoding="utf-8")

    def test_basics(self):
        self.assertEqual(self.data["format"], 1)
        self.assertEqual(self.data["prepare"]["version"], self.version,
                         "bump prepare.version with VERSION so players get the new build")
        for prog in self.data["prepare"]["program"]:
            self.assertTrue((ROOT / prog).is_file(), prog)

    def test_low_keys_exist(self):
        # every [low] key a setting writes is one the game knows
        low = self.toml.split("[low]", 1)[1]
        for s in self.data["settings"]:
            for choice in s["choices"]:
                for key in choice.get("set", {}):
                    if key.startswith("low/"):
                        self.assertRegex(low, r"(?m)^" + re.escape(key[4:]) + r"\s*=", key)

    def test_settings_consistent(self):
        groups = {g["id"] for g in self.data.get("groups", [])}
        ids = [s["id"] for s in self.data["settings"]]
        self.assertEqual(len(ids), len(set(ids)))
        for s in self.data["settings"]:
            self.assertGreaterEqual(len(s["choices"]), 2, s["id"])
            if "group" in s:
                self.assertIn(s["group"], groups, s["id"])
            for choice in s["choices"]:
                for key in choice.get("set", {}):
                    self.assertIn(key, s.get("defaults", {}), f"{s['id']}: no default for {key}")

    def test_release_zip(self):
        with tempfile.TemporaryDirectory() as out:
            subprocess.run([sys.executable, str(ROOT / "tools" / "make_release.py"), "--out", out],
                           check=True, capture_output=True)
            zips = list(Path(out).glob("*.zip"))
            self.assertEqual(len(zips), 1)
            self.assertRegex(zips[0].name, r"-Windows-x64\.zip$")
            top = f"LoWRecomp-v{self.version}/"
            names = zipfile.ZipFile(zips[0]).namelist()
            for must in ("recomplauncher.json", "install.bat", "installer/prepare.cmd",
                         "installer/Install-LoWRecomp.ps1", "project/CMakeLists.txt"):
                self.assertIn(top + must, names)
            self.assertFalse([n for n in names if n.lower().endswith((".iso", ".xex", ".pac", ".exe"))])


if __name__ == "__main__":
    unittest.main()
