# Reproducing local static analysis

Use only a binary the owner has authorized for analysis. This workflow reads the target and keeps isolated copies/projects/outputs under ignored `artifacts/`. It does not execute the target. Public source and tests do not depend on the reference application.

## Inventory and original fixtures

From the repository root:

```sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 scripts/inspect_macho.py /path/to/authorized/binary --output /path/to/new-local-inventory.json
```

The inventory destination must not exist. Universal offsets are checked relative to their slices. Counts, fixed Mach-O metadata and declared function-start addresses are included by default; `--include-named-functions` additionally exports surviving named text symbols and should remain local. A declared start or symbol is not a correctness certificate for a recovered function.

For the optional Ghidra integration test, select an existing installation with working native components and its compatible JDK:

```sh
GHIDRA_HOME=/path/to/ghidra JAVA_HOME=/path/to/jdk \
  python3 tests/test_ghidra_export.py --timeout 120
```

This compiles tiny original ARM64 C fixtures on macOS, exports two functions, checks their arithmetic/hashes and verifies invalid-address, duplicate-address and overwrite behavior. Without `GHIDRA_HOME` it explicitly skips. It installs nothing. The timeout is a total wall-clock budget across compilation and Ghidra subprocesses.

## Tool preparation

The verified combination is Ghidra 12.1.4, JDK 21 and locally built Mac ARM64 native components. Download the release asset from the [official release](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build), verify its published SHA-256, extract into a fresh local directory, and follow that release's `GettingStarted.html`. Its supplied wrapper can build native components from `support/gradle` with `./gradlew buildNatives`.

Use the released documentation, not a moving development README with different Java requirements. An extracted ZIP must retain executable permissions on launcher scripts. No global skill installation or MCP server is required. See the [reviewed skill and its limitations](reverse-engineering-skills.md).

## Import once, preserve the project

On macOS, `xcrun lipo` can create an isolated ARM64 slice from an authorized universal binary. Use a new destination, retain hashes for both the universal file and the slice, and keep the installed application unchanged. A thin ARM64 input needs no slice extraction.

Set `GHIDRA_HOME` and `JAVA_HOME` in your shell to the existing tool and JDK directories. Choose an existing local project directory and a new project name, then use the Ghidra launcher:

```sh
GHIDRA_HEADLESS_MAXMEM=4G "$GHIDRA_HOME/support/analyzeHeadless" \
  /path/to/local/projects ResearchProject \
  -import /path/to/isolated-arm64.macho -noanalysis -max-cpu 2
```

`-noanalysis` is intentional: first inventory and select a narrow question. Keep the project for subsequent passes; do not use `-deleteProject`. The loader-selected language and compiler specification belong in the evidence. A loader's compiler label is not proof of original build settings. Imported external-library names are unresolved boundaries unless separately investigated.

## Export explicit entries with coverage

Choose entry addresses from the selected slice's declared start table and verify their provenance. Do not interpret a name match alone as a verified implementation. The output parent directory must exist, and the final output directory must be new:

```sh
GHIDRA_HEADLESS_MAXMEM=4G "$GHIDRA_HOME/support/analyzeHeadless" \
  /path/to/local/projects ResearchProject \
  -process isolated-arm64.macho -noanalysis -max-cpu 2 \
  -scriptPath "$PWD/scripts/ghidra" \
  -postScript ExportSelectedFunctions.java \
  /path/to/new-local-output 0xENTRY_ADDRESS
```

Replace the address placeholder with one or more explicit hexadecimal entries. The original exporter uses address keys, preserves a per-entry outcome and never silently truncates a name-keyed graph. It disables broad automatic analysis, bounds preparation and decompilation separately to 30 seconds per entry, saves body ranges/hashes, and emits one `.c` only after a successful decompiler result.

Read `manifest.json`, not just the shell exit status: Ghidra can return process success after a script failure. Check requested/completed/failed/pending counts, timeout and cancellation flags, error messages, nonempty output and matching program hash. An incomplete run may leave pending entries in its last atomic manifest. Repeat with a new output directory instead of overwriting earlier evidence. Long lists still multiply per-entry work; keep selections purposeful.

## Resumable inventory sweep

The inventory-wide runner is separate from the explicit-entry exporter. It holds an exclusive coordinator lock for the Ghidra project and a separate output lock. Do not run another modifying script against that project while a sweep owns it. The runner never imports or installs tools itself.

```sh
GHIDRA_HOME=/path/to/ghidra JAVA_HOME=/path/to/jdk GHIDRA_HEADLESS_MAXMEM=4G \
  python3 scripts/bulk_decompile.py \
  --inventory /path/to/local-inventory.json --architecture arm64 \
  --binary /path/to/isolated-arm64.macho \
  --project-dir /path/to/local/projects --project-name ResearchProject \
  --program isolated-arm64.macho --output /path/to/new-local-sweep \
  --batch-size 1024 --max-batches 384 --process-timeout 600
```

The manifest binds the binary hash, inventory hash, architecture, complete address count, project/program identity and exporter hash. The SQLite coverage index distinguishes completed, failed, timed out, interrupted and unattempted addresses. Every batch has a create-only request and per-address durable JSONL events; successful pseudocode files have content hashes. Complete malformed records, duplicate JSON keys and mismatched output hashes fail validation. A trailing partial record from interruption is retained as evidence.

Repeat the same arguments to resume unfinished entries in new batch folders. Existing completed outputs are not overwritten. Explicit per-function failures/timeouts remain recorded rather than silently retried. A changed exporter requires a new output identity; preserve the earlier pass to compare recovery. An interrupted or failed process stops that invocation so its evidence can be reviewed before resuming. `--status` checks provenance and reports indexed counts without launching Ghidra, but also takes the project lock; use it between invocations, not concurrently with the sweep.

Preparation accepts an existing decoded instruction and lets Ghidra reconcile a new declared entry with an overlapping provisional function. The result records the prior containing model where applicable. This improves preparation coverage but does not prove the resulting boundary is exact. Per-address preparation/decompilation budgets and the total batch timeout remain independent; bounded settings may leave difficult entries unresolved.

The optional original-fixture integration suite exercises real interruption/resume, predecoded instructions and shared-tail overlap recovery:

```sh
GHIDRA_HOME=/path/to/ghidra JAVA_HOME=/path/to/jdk \
  python3 -m unittest discover -s tests -p 'test_bulk_decompile.py' -v
```

Without Ghidra on macOS, the native integration fixture explicitly skips while all pure coverage/provenance tests still run on macOS, Windows and Linux. Generated fixture pseudocode and proprietary target output remain outside public source.


## Read-only verification across retries

Keep the original pass and every retry selection immutable. A retry uses a separately named output directory and an inventory explicitly containing only its selected addresses; do not change the original denominator. With the same architecture and program hash, verify one or more indexed snapshots without acquiring the analysis-project lock:

```sh
python3 scripts/verify_decompile_coverage.py \
  --inventory /path/to/full-inventory.json --architecture arm64 \
  --run /path/to/baseline /path/to/full-inventory.json \
  --run /path/to/retry /path/to/retry-selection.json \
  --report /path/to/new-local-verification.json
```

Supply runs in intended evidence order. The verifier checks the exact selection and recorded program identities, validates indexed address order, freezes each read-only database snapshot, closes it, and hashes every completed output. Missing, empty, altered, misnamed or symlinked outputs fail verification. It does not ingest active journals; unindexed batch IDs are retained in the report. Separate runs have separate snapshot timestamps, not a claimed simultaneous whole-system snapshot.

A completed address counts once even if multiple runs produced outputs, and every such output must still pass its hash check. For addresses without any verified success, the latest attempted status wins; an unattempted selection cannot erase a prior result. Per-run outcome counts and frozen-row hashes preserve the underlying evidence. New reports never overwrite old ones. Keep reports private alongside the source artifacts; they contain local paths and analysis identities. Pure synthetic tests cover overlapping retries, stale failures, corrupt provenance/output, interrupted and unindexed work, symlink rejection where permitted, and a concurrent writer during file hashing.

## Interpretation and implementation

Pseudocode can have incorrect types, incomplete indirect-call targets, merged/tail-shared code and compiler artifacts. Manifest body ranges and their hashes describe Ghidra's current function model, not a proven complete set of reachable instructions. In the retained no-analysis pass, indirect branch targets fell outside that model even though they appeared in the decompiler output; the whole-program hash remains the complete input identity. Keep byte addresses and hashes when refining a hypothesis. Corroborate important observations with an independent disassembly or controlled runtime/render experiment. Do not publish raw target code or treat it as original Halftrack source.

Turn each useful finding into an independently written observable contract and generated test. Distinguish tool success, known-good Halftrack output, paired reference comparison and physical hardware validation. The [current results](reverse-engineering-results.md) record exactly what has been done; the [measurement report](behavioral-parity-measurement.md) describes the remaining comparison work.
