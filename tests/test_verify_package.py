#!/usr/bin/env python3
"""The package verifier: a synthetic install whose source, bundle and --version line agree passes,
and each kind of disagreement (missing license, stale bundle version, a build from another or a
modified commit, an unreadable version line) is reported."""

import importlib.util
import io
import json
from pathlib import Path
import plistlib
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("verify_package", ROOT / "scripts" / "verify_package.py")
verify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verify)

COMMIT = "0123456789abcdef0123456789abcdef01234567"


def install(root, system, version="1.2.3", bundle_version=None):
    """A package folder as package_preview.py lays it out, with an empty application."""
    for relative in verify.REQUIRED:
        (root / relative).parent.mkdir(parents=True, exist_ok=True)
        (root / relative).write_bytes(b"")
    cmake = f"cmake_minimum_required(VERSION 3.25)\nproject(Halftrack VERSION {version} LANGUAGES C CXX)\n".encode()
    with tarfile.open(root / "source" / "halftrack.tar", "w") as archive:
        info = tarfile.TarInfo("CMakeLists.txt"); info.size = len(cmake)
        archive.addfile(info, io.BytesIO(cmake))
    (root / "SOURCE.json").write_text(json.dumps({"halftrack": f"https://github.com/example/halftrack/tree/{COMMIT}"}))
    executable, plist = verify.application(root, system)
    executable.parent.mkdir(parents=True, exist_ok=True)
    executable.write_bytes(b"")
    if plist is not None:
        plist.write_bytes(plistlib.dumps({"CFBundleIdentifier": "org.halftrack.studio",
                                          "CFBundleShortVersionString": bundle_version or version, "CFBundleVersion": bundle_version or version}))


class PackageVerifier(unittest.TestCase):
    def check(self, system, line, **layout):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            install(root, system, **layout)
            return verify.check_install(root, system, lambda executable: line)

    def test_project_version(self):
        self.assertEqual(verify.project_version("project(Halftrack VERSION 0.1.0 LANGUAGES C CXX)"), "0.1.0")
        self.assertEqual(verify.project_version((ROOT / "CMakeLists.txt").read_text()).count("."), 2)
        self.assertIsNone(verify.project_version("project(Other VERSION 2.0)"))

    def test_agreeing_install_passes(self):
        for system, name in (("Darwin", "macOS arm64"), ("Windows", "Windows x86_64"), ("Linux", "Linux x86_64")):
            problems, facts = self.check(system, f"Halftrack 1.2.3 (0123456, {name})")
            self.assertEqual(problems, [], system)
            self.assertEqual((facts["version"], facts["commit"]), ("1.2.3", COMMIT))

    def test_disagreements_are_reported(self):
        self.assertTrue(any("Info.plist says 1.2.2" in p for p in self.check("Darwin", "Halftrack 1.2.3 (0123456, macOS arm64)", bundle_version="1.2.2")[0]))
        self.assertTrue(any("commit 0123456-modified" in p for p in self.check("Linux", "Halftrack 1.2.3 (0123456-modified, Linux x86_64)")[0]))
        self.assertTrue(any("commit fedcba9" in p for p in self.check("Linux", "Halftrack 1.2.3 (fedcba9, Linux x86_64)")[0]))
        self.assertTrue(any("names 1.2.4" in p for p in self.check("Windows", "Halftrack 1.2.4 (0123456, Windows x86_64)")[0]))
        self.assertTrue(any("names Linux" in p for p in self.check("Windows", "Halftrack 1.2.3 (0123456, Linux x86_64)")[0]))
        self.assertTrue(any("printed" in p for p in self.check("Linux", "Segmentation fault")[0]))

    def test_missing_license_is_reported(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            install(root, "Linux")
            (root / "LICENSE-JUCE.md").unlink()
            problems, _ = verify.check_install(root, "Linux", lambda executable: "Halftrack 1.2.3 (0123456, Linux x86_64)")
            self.assertEqual(problems, ["missing LICENSE-JUCE.md"])


if __name__ == "__main__":
    unittest.main()
