#!/usr/bin/env python3
"""The evidence report: the evidence map covers every PARTIAL ledger row with suites that exist,
and a run's JUnit results become each row's expected and actual results."""

import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("evidence_report", ROOT / "scripts" / "evidence_report.py")
evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evidence)

LEDGER = """| ID | Capability | Test | Status |
| --- | --- | --- | --- |
| AUD-001 | CoreAudio backend | Measure it. | PARTIAL — on a device |
| AUD-002 | ASIO | Measure it. | NOT IMPLEMENTED / UNVERIFIED |
| PRE-001 | Preview | Check it. | PARTIAL |
| ARR-008 | Fades | Fade it. | PARTIAL — curves |
"""
CMAKE = """add_test(NAME crossfade-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-crossfade ${CMAKE_BINARY_DIR}/test-artifacts)
add_test(NAME native-effects-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-effects ${CMAKE_BINARY_DIR}/test-artifacts)
add_test(NAME session-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test ${CMAKE_BINARY_DIR}/test-artifacts)
"""
MAP = """| Row | Suites | Manual |
| --- | --- | --- |
| AUD-001 |  | measured on a device by hand |
| ARR-008 | crossfade, session |  |
"""
JUNIT = """<?xml version="1.0"?><testsuite><testcase name="crossfade-acceptance" status="run" time="1.5"><system-out>PASS crossfade: one
PASS crossfade: two
Cleaning up</system-out></testcase>
<testcase name="session-acceptance" status="fail" time="60"><failure message="x"/></testcase></testsuite>"""


class EvidenceReport(unittest.TestCase):
    def test_rows_and_suites(self):
        self.assertEqual(evidence.ledger_rows(LEDGER), {"AUD-001": "CoreAudio backend", "ARR-008": "Fades"})
        self.assertEqual(evidence.suites(CMAKE), {"crossfade": "crossfade", "native-effects": "effects", "session": ""})

    def test_a_complete_map_passes(self):
        self.assertEqual(evidence.check(LEDGER, MAP, CMAKE), [])

    def test_gaps_are_reported(self):
        missing = MAP.replace("| ARR-008 | crossfade, session |  |\n", "")
        self.assertEqual(evidence.check(LEDGER, missing, CMAKE), ["ARR-008 is PARTIAL in the ledger but not in the evidence map"])
        unknown = MAP.replace("crossfade, session", "crossfades")
        self.assertEqual(evidence.check(LEDGER, unknown, CMAKE), ["ARR-008 names crossfades, which is not a suite"])
        empty = MAP.replace("measured on a device by hand", "")
        self.assertEqual(evidence.check(LEDGER, empty, CMAKE), ["AUD-001 names no suite and no manual evidence"])
        extra = MAP + "| AUD-002 | crossfade |  |\n"
        self.assertEqual(evidence.check(LEDGER, extra, CMAKE), ["AUD-002 is in the evidence map but not PARTIAL in the ledger"])

    def test_a_run_becomes_expected_and_actual_results(self):
        data = evidence.report(LEDGER, MAP, CMAKE, evidence.junit_results(JUNIT), "ctest")
        rows = {row["row"]: row for row in data["rows"]}
        self.assertEqual(rows["AUD-001"]["evidence"], [])
        self.assertEqual(rows["AUD-001"]["manual"], "measured on a device by hand")
        fades = rows["ARR-008"]
        self.assertEqual([(e["suite"], e["expected"], e["actual"]) for e in fades["evidence"]], [("crossfade", "passed", "passed"), ("session", "passed", "failed")])
        self.assertEqual(fades["evidence"][0]["command"], "Halftrack --self-test-crossfade <artifacts>")
        self.assertEqual(fades["evidence"][1]["command"], "Halftrack --self-test <artifacts>")
        self.assertFalse(fades["verified"])
        self.assertIn("commit", data["environment"])
        self.assertEqual([e["checks"] for e in fades["evidence"]], [2, 0])
        self.assertIn("| ARR-008 | Fades | crossfade (passed, 2 checks), session (failed) |  |", evidence.markdown(data))

    def test_the_repository_map_is_current(self):
        problems = evidence.check((ROOT / "docs" / "feature-parity.md").read_text(encoding="utf-8"), (ROOT / "docs" / "evidence-map.md").read_text(encoding="utf-8"),
                                  (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        self.assertEqual(problems, [], "update docs/evidence-map.md")


if __name__ == "__main__":
    unittest.main()
