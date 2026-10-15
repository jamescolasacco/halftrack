# Recorded implementation coverage

Current implementation checkpoint: MIDI interchange implementation, 2026-10-15. **27 of 246 tracked active feature areas have partial implementation evidence: 11.0%.** None of these broad rows establishes complete measured reference parity.

This is recorded partial coverage, not percentage finished, remaining engineering time, or a weighted feature score. The initial inventory is not exhaustive, some existing behavior is recorded only in preview rows, and items vary greatly in size. Static decompiler coverage uses a different denominator and must never be reported as application completion.

## Reproducible denominator

The [capability ledger](feature-parity.md) has 275 identified rows. Excluding PRE preview-check rows, OUT scope/watchlist rows, EXT programmable-environment rows and rows explicitly OUT OF SCOPE leaves 246 active rows. Count each active row whose status contains PARTIAL once. No fractional credit or estimated effort weighting is assigned.

The partial rows are ARR-003/005/006/008/009/011/012, SES-001/002/008, WRP-001, MID-001/002/006/012, MIX-003/007, PRJ-002/005, UX-001/002, QA-003 and AFX-009/011/015/032/040. The [progress log](progress.md), ledger and per-feature contracts record what each actually does and what remains missing.

Before audio editing, source `b342496` had 19 partial rows with the same denominator: 7.7%. The six added rows are ARR-003/005/006/008/012 and WRP-001. Their evidence and boundaries are in the [audio-editing contract](research/audio-clip-editing-contract.md). Timeline gestures improve those existing rows without increasing their count.

Clipboard, direct range gestures and cross-track movement at `fc70170` deepen the existing ARR-003/005 rows. They do not add new broad rows or change the 25/246 partial-coverage count. This explicitly avoids treating each control or test as another percentage point of whole-product completion.

Named absolute locators add partial evidence for ARR-009. They bring the count to 26/246; this is documented feature-area coverage, not a finished-clone estimate.

MIDI interchange adds partial evidence for MID-012. Audio reversal deepens WRP-001 without adding a new row. The total is 27/246 (11.0% recorded partial coverage), not a percentage of a completed clone.
