#!/usr/bin/env python3
r"""Opt-in integration test using an original, temporary macOS ARM64 C fixture.

GHIDRA_HOME must point to an existing Ghidra installation with working native
components. JAVA_HOME may select its compatible JDK. Nothing is installed, and
no proprietary executable is inspected. Unset GHIDRA_HOME explicitly skips this
test during ordinary unittest discovery.

Example:
    GHIDRA_HOME=/path/to/ghidra JAVA_HOME=/path/to/jdk \
        python3 tests/test_ghidra_export.py --timeout 120
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


TOTAL_TIMEOUT_SECONDS = 120.0
REPOSITORY = Path(__file__).resolve().parents[1]
FIXTURE_SOURCE = """\
__attribute__((noinline)) int fixture_affine(int value) { return value * 3 + 7; }
__attribute__((noinline)) float fixture_gain(float value) { return value * 0.25f; }
int main(int argc, char **argv) {
    (void)argv;
    return fixture_affine(argc) + (int)fixture_gain((float)argc);
}
"""


class SelectedFunctionExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        configured = os.environ.get("GHIDRA_HOME")
        if not configured:
            raise unittest.SkipTest("GHIDRA_HOME unset: Ghidra integration is explicitly opt-in")
        if sys.platform != "darwin":
            raise unittest.SkipTest("This integration fixture currently requires macOS and its native toolchain")
        cls.headless = Path(configured).expanduser().resolve() / "support" / "analyzeHeadless"
        if not cls.headless.is_file():
            raise unittest.SkipTest(f"Ghidra headless launcher is missing: {cls.headless}")
        cls.xcrun = shutil.which("xcrun")
        if not cls.xcrun:
            raise unittest.SkipTest("macOS xcrun is missing; no tools are installed automatically")

    def invoke(self, args, log_path):
        remaining = self.deadline - time.monotonic()
        self.assertGreater(remaining, 0, "Total integration-test timeout exhausted")
        try:
            result = subprocess.run(
                [str(value) for value in args], env=self.environment,
                capture_output=True, text=True, timeout=remaining, check=False)
        except subprocess.TimeoutExpired as error:
            self.fail(f"Command exceeded the remaining total timeout: {error.cmd[0]}")
        output = result.stdout + result.stderr
        log_path.write_text(output, encoding="utf-8")
        return result, output

    def test_export_results_and_output_guards(self):
        self.deadline = time.monotonic() + TOTAL_TIMEOUT_SECONDS
        self.environment = dict(os.environ, GHIDRA_HEADLESS_MAXMEM="4G")
        with tempfile.TemporaryDirectory(prefix="halftrack-ghidra-integration-") as temporary:
            directory = Path(temporary)
            source = directory / "fixture.c"
            binary = directory / "fixture"
            source.write_text(FIXTURE_SOURCE, encoding="utf-8")
            compiled, output = self.invoke(
                [self.xcrun, "clang", "-arch", "arm64", "-O1", "-fno-inline",
                 "-o", binary, source], directory / "compile.log")
            self.assertEqual(compiled.returncode, 0, output)
            listed, output = self.invoke(
                [self.xcrun, "nm", "-n", binary], directory / "symbols.log")
            self.assertEqual(listed.returncode, 0, output)
            symbols = {}
            for line in output.splitlines():
                fields = line.split()
                if len(fields) == 3 and fields[2] in ("_fixture_affine", "_fixture_gain"):
                    symbols[fields[2]] = f"0x{int(fields[0], 16):x}"
            self.assertEqual(set(symbols), {"_fixture_affine", "_fixture_gain"})
            entries = [symbols["_fixture_affine"], symbols["_fixture_gain"]]
            projects = directory / "projects"
            projects.mkdir()
            export = directory / "export"
            launcher = [self.headless, projects, "Fixture"]
            options = ["-noanalysis", "-max-cpu", "2", "-scriptPath",
                       REPOSITORY / "scripts" / "ghidra", "-postScript",
                       "ExportSelectedFunctions.java"]
            result, output = self.invoke(
                launcher + ["-import", binary] + options + [export] + entries + ["0xdeadbeef"],
                directory / "export.log")
            self.assertEqual(result.returncode, 0, output[-8000:])
            manifest_path = export / "manifest.json"
            self.assertTrue(manifest_path.is_file(), output[-8000:])
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual((manifest["requested_count"], manifest["completed_count"],
                              manifest["failed_count"]), (3, 2, 1))
            self.assertEqual(manifest["program_sha256"], hashlib.sha256(binary.read_bytes()).hexdigest())
            self.assertFalse(manifest["broad_auto_analysis"])
            self.assertEqual(manifest["decompile_timeout_seconds"], 30)
            self.assertIn("AARCH64", manifest["language_id"])
            self.assertTrue(manifest["ghidra_version"])
            self.assertTrue(manifest["compiler_spec_id"])
            texts = []
            for entry in entries:
                function = manifest["functions"][entry]
                self.assertEqual(function["status"], "completed", function)
                self.assertFalse(function["timed_out"])
                self.assertFalse(function["cancelled"])
                self.assertTrue(function["created_function"])
                self.assertGreater(function["body_bytes"], 0)
                self.assertEqual(sum(part["bytes"] for part in function["body_ranges"]),
                                 function["body_bytes"])
                self.assertEqual(len(function["body_sha256"]), 64)
                contents = (export / function["output_file"]).read_bytes()
                self.assertEqual(function["output_file"], entry + ".c")
                self.assertEqual(function["output_sha256"], hashlib.sha256(contents).hexdigest())
                text = contents.decode("utf-8")
                self.assertGreater(len(text.strip()), 20)
                self.assertIn("return", text)
                texts.append(text)
            self.assertRegex(texts[0], r"\*\s*3\s*\+\s*7")
            self.assertRegex(texts[1], r"\*\s*0\.25(?:[fF])?\s*;")
            failed = manifest["functions"]["0xdeadbeef"]
            self.assertEqual(failed["status"], "failed")
            self.assertEqual(failed["stage"], "prepare")
            self.assertEqual(failed["error"], "Entry is not in initialized executable memory")
            self.assertNotIn("output_file", failed)
            self.assertFalse((export / "0xdeadbeef.c").exists())

            # Ghidra can exit zero after a script failure: inspect its diagnostic
            # and preserved files, rather than mistaking process success for export success.
            before = {file.name: file.read_bytes() for file in export.iterdir()}
            process = launcher + ["-process", "fixture"] + options
            _, output = self.invoke(process + [export, entries[0]], directory / "overwrite.log")
            self.assertIn("FileAlreadyExistsException", output)
            self.assertEqual(before, {file.name: file.read_bytes() for file in export.iterdir()})
            duplicate_output = directory / "duplicate"
            _, output = self.invoke(
                process + [duplicate_output, entries[0], entries[0][2:]], directory / "duplicates.log")
            self.assertIn("Duplicate entry address", output)
            self.assertFalse(duplicate_output.exists())
            self.assertTrue((projects / "Fixture.gpr").is_file(), "Fixture project must remain saved")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="total wall-clock budget in seconds for compilation and all Ghidra runs (default: 120)")
    args = parser.parse_args()
    if not 0 < args.timeout <= 3600:
        parser.error("--timeout must be positive, finite, and at most 3600 seconds")
    global TOTAL_TIMEOUT_SECONDS
    TOTAL_TIMEOUT_SECONDS = args.timeout
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SelectedFunctionExportTests)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
