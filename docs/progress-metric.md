# Recorded implementation coverage

Snapshot: source b342496, 2026-10-14. The ledger has 275 rows. Excluding PRE preview-check rows, OUT scope/watchlist rows, EXT programmable-environment rows and rows explicitly OUT OF SCOPE leaves **246 active rows**. **19 have PARTIAL evidence: 7.7%.** None of these broad rows establishes complete measured reference parity.

This is recorded partial coverage, not percentage finished, remaining engineering time, or a weighted feature score. The initial inventory is not exhaustive, some existing behavior is recorded only in preview rows, and items vary greatly in size. Static decompiler coverage uses a different denominator and must never be reported as application completion.

The recorded partial rows at this checkpoint are ARR-011, SES-001/002/008, MID-001/002/006, MIX-003/007, PRJ-002/005, UX-001/002, QA-003 and AFX-009/011/015/032/040. Current implementation and remaining boundaries are in [progress](progress.md) and the [capability ledger](feature-parity.md).

The audio-editing continuation adds partial evidence for ARR-003/005/006/008/012 and WRP-001. With the same denominator, **25 of 246 rows have partial evidence: 10.2% recorded partial coverage**. This still is not a percentage of the clone completed. The added evidence and remaining boundaries are in the [audio-editing contract](research/audio-clip-editing-contract.md).
