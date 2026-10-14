# Offline export latency boundary regression

Verified locally on Apple Silicon macOS on 2026-10-13. The original test VST3 implements and reports a 127-sample stereo delay; it contains no vendor DSP. Direct fixture processing is exact across block sizes 1, 63, 256 and 511, and reset clears retained samples.

The original host test used interior bursts with silent margins. It passed, but a stronger fixture with nonzero opening and closing samples exposed incorrect compensation at the start of the exported file. The strengthened test compares every sample without shifting or cropping the result, validates exact frame count, and rejects nonfinite reference or actual samples.

## Cause and controlled verification

Two errors were present in the pinned Tracktion offline renderer:

1. `writeAudioBlock` converted a sliced channel view to a JUCE buffer without its frame offset. After dropping latency, writing restarted from the original channel bases. A varying stereo fixture failed at 127 samples of plug-in delay with maximum error 0.094726443 (8.79-second test run).
2. The latency-drop branch computed its offset from the full 512-frame buffer rather than the number actually dropped. Applying only the first correction made all one-second edge cases pass, including 635 samples of delay, but a 64-frame export still failed with maximum error 0.093750000 (14.14-second run). This independently reproduces the short-block arithmetic defect.

`cmake/PatchRenderLatency.cmake` applies the two narrow corrections to exact-pinned source: carry the frame offset into the JUCE buffer and set the slice start to the actual dropped count. It changes the offline worker, not the real-time callback. Existing upstream notices remain. Exact hashes are recorded in the dependency inventory.

The complete latency acceptance test now passes in **17.40 seconds**. Ten boundary exports cover 48,000- and 64-frame stereo clips with 127, 254, 381, 508 and 635 samples of serial delay. The 508 case retains four frames in the first processing block; 635 crosses a whole block before trimming the next. Seven existing interior/parallel/reopen/bypass/remove cases remain. The comparison tolerance is 2e-6 for 24-bit output. A dry render is the edge oracle, preserving native clip-edge behavior rather than assuming a different envelope.

Disposable-source CMake checks also pass initial application, repeat application, rejection of unknown source without mutation, and CRLF helper input producing the exact LF output hash. No temporary diagnostic logging is retained. Cross-platform results for this correction are pending the new CI run; this evidence does not establish arbitrary third-party plug-in or physical driver latency behavior.
