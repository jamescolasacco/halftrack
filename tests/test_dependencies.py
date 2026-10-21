#!/usr/bin/env python3
"""The dependency manifest matches the build: every patch script is applied and described, its
hashes and the pinned archives are in the generated index, and Halftrack's own license is AGPLv3."""

import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("dependency_index", ROOT / "scripts" / "dependency_index.py")
index = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(index)


class DependencyManifest(unittest.TestCase):
    def setUp(self):
        self.doc = (ROOT / "docs" / "dependencies.md").read_text()
        self.cmake = (ROOT / "CMakeLists.txt").read_text()
        self.scripts = sorted((ROOT / "cmake").glob("Patch*.cmake"))

    def test_generated_index_is_current(self):
        self.assertIn(index.section(), self.doc, "run: python scripts/dependency_index.py --write")

    def test_pinned_archives_are_exact(self):
        archives = index.archives()
        self.assertEqual({name for name, _, _ in archives}, {"juce", "tracktion"})
        for _, revision, digest in archives:
            self.assertRegex(revision, r"^[0-9a-f]{40}$", "pins name a full commit")
            self.assertIn(revision, self.doc.split(index.START)[0], "the revision is in the dependency table")
            self.assertRegex(digest, r"^[0-9a-f]{64}$")

    def test_every_patch_is_applied_and_described(self):
        prose = self.doc.split(index.START)[0]
        for script in self.scripts:
            with self.subTest(script=script.name):
                self.assertIn(f"include(cmake/{script.name})", self.cmake, "the build applies it")
                functions = re.findall(r"^function\((\w+)", script.read_text(), re.MULTILINE)
                self.assertTrue(functions and all(f + "(" in self.cmake for f in functions), "its functions are called")
                self.assertIn(script.name, prose, "the manifest describes it")
                self.assertTrue(index.patch_hashes(script), "it is gated by exact hashes")

    def test_halftrack_is_agpl(self):
        self.assertTrue((ROOT / "LICENSE").read_text().lstrip().startswith("GNU AFFERO GENERAL PUBLIC LICENSE"))
        self.assertIn("AGPL-3.0-only", self.doc)


if __name__ == "__main__":
    unittest.main()
