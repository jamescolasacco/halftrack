# Regression evidence

Status: partial, QA-013.

Each capability with partial implementation should be tied to the evidence that checks it, so that a regression names the capability it breaks.

## The map

[docs/evidence-map.md](../evidence-map.md) lists every PARTIAL row of the capability ledger with the automated suites that exercise it. Suites are CTest names without "-acceptance": "session" is the Session suite and "python" the tooling tests. Where a row rests on hand checks (the MOTU M4 measurements, CI on three systems, the package check), the map says so.

The first map was drawn from each row's text and contract, and from the tests those name. A suite's source files come from `src/Main.cpp`'s self-test dispatch. The rows those named nothing for were filled in by hand from the suites' subjects.

`scripts/evidence_report.py --check` fails when:

- a PARTIAL row is missing from the map;
- the map lists a row that is not PARTIAL;
- the map names a suite CMakeLists.txt does not define;
- a row names no suite and no manual evidence.

`tests/test_evidence_report.py` runs that check against the repository with the tooling tests, so the map stays in step with the ledger and the build.

## A run's evidence

In CI, CTest writes its results as JUnit (`build/test-artifacts/ctest.xml`). `evidence_report.py --junit` then writes `evidence.json` and `evidence.md`, uploaded with the other test artifacts. The report holds:

- the commit (and whether tracked files were modified);
- the system, release and machine;
- the command that ran the tests;
- for each row, each suite's self-test command, expected result (passed), actual result and duration;
- whether every named suite passed.

A row whose suites did not all pass is listed at the end.

## Not yet covered

- Fixtures by name: each suite generates its own, described in its contract, but the report does not list them.
- Per-check results within a suite: the suites print each check's PASS line, which CTest keeps only in its log.
- History across runs.
- Local runs, which use the gate script rather than CTest.
