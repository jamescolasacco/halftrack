# Recorded implementation coverage

Current implementation checkpoint: source `0a6e7e1`, 2026-10-14. **25 of 246 tracked active feature areas have partial implementation evidence: 10.2%.** None of these broad rows establishes complete measured reference parity.

This is recorded partial coverage, not percentage finished, remaining engineering time, or a weighted feature score. The initial inventory is not exhaustive, some existing behavior is recorded only in preview rows, and items vary greatly in size. Static decompiler coverage uses a different denominator and must never be reported as application completion.

## Reproducible denominator

The [capability ledger](feature-parity.md) has 275 identified rows. Excluding PRE preview-check rows, OUT scope/watchlist rows, EXT programmable-environment rows and rows explicitly OUT OF SCOPE leaves 246 active rows. Count each active row whose status contains PARTIAL once. No fractional credit or estimated effort weighting is assigned.

The partial rows are ARR-003/005/006/008/011/012, SES-001/002/008, WRP-001, MID-001/002/006, MIX-003/007, PRJ-002/005, UX-001/002, QA-003 and AFX-009/011/015/032/040. The [progress log](progress.md), ledger and per-feature contracts record what each actually does and what remains missing.

Before audio editing, source `b342496` had 19 partial rows with the same denominator: 7.7%. The six added rows are ARR-003/005/006/008/012 and WRP-001. Their evidence and boundaries are in the [audio-editing contract](research/audio-clip-editing-contract.md). Timeline gestures improve those existing rows without increasing their count.
