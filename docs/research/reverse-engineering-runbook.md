# Reproducing local static analysis

Use only a binary the owner has authorized for analysis. This workflow reads the target and keeps isolated copies/projects/outputs under ignored `artifacts/`. It does not execute the target. Public source and tests do not depend on the reference application.

## Inventory and original fixtures

From the repository root:

```sh
python3 -m unittest discover -s tests -p 'test_macho_inventory.py' -v
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

## Interpretation and implementation

Pseudocode can have incorrect types, incomplete indirect-call targets, merged/tail-shared code and compiler artifacts. Keep byte addresses and hashes when refining a hypothesis. Corroborate important observations with an independent disassembly or controlled runtime/render experiment. Do not publish raw target code or treat it as original Halftrack source.

Turn each useful finding into an independently written observable contract and generated test. Distinguish tool success, known-good Halftrack output, paired reference comparison and physical hardware validation. The [current results](reverse-engineering-results.md) record exactly what has been done; the [measurement report](behavioral-parity-measurement.md) describes the remaining comparison work.
