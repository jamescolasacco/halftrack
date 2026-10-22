# Similar sounds

Status: partial, PRJ-010. Halftrack's own measure of similarity, not the reference's; the reference was used only as a library of categorised sounds to evaluate it against.

## The measure

`similarity::analyse` (`src/SoundSimilarity.*`) reads up to the first ten seconds of a sound, mixed to mono, and measures 27 values. There are eleven measures:

- its length (of the whole file) and crest factor;
- the time its 5 ms envelope takes to reach 90% of its peak;
- how much of its energy after the peak comes in the second half;
- how much of the peak remains in its last third;
- the spectral centroid, from 2048-point Hann frames with half overlap, with its spread;
- spectral flatness and 85% rolloff;
- the zero-crossing rate;
- spectral flux.

The other sixteen are the share of its energy in each of 16 log-spaced bands from 30 Hz to 18 kHz, in decibels.

Silence (peak below 1e-5), missing files and files that are not audio have no features. Every value is finite, and the same file always measures the same.

`similarity::Library` compares sounds on the library's own scale. Each value is divided by its spread across the library (values the library does not vary in are left out), and sounds rank by Euclidean distance, nearest first. Ties break by path.

## In the browser

A sample's right-click menu has **Show Similar Files**. The browser then lists the sound, followed by the 50 sounds under the browser's folder that are nearest it, under "SIMILAR TO <NAME>". `similarity::FolderIndex` measures the folder's sounds (at most 5000) on a worker thread; the list fills in as they are measured, and the header counts them. The worker shares only its own state, so a read blocked in the filesystem can outlive the browser. Measurements are kept by path, size and modification time, so asking again about the same folder measures only what changed. A search or another folder leaves the list.

## Evaluation

**A generated library** (`tests/SimilarityTests.cpp`, in CI) holds eight each of kicks, toms, snares, claps, hi-hats, basses, plucks and pads, drawn from a fixed seed. The pairs (kick and tom, snare and clap, pluck between bass and pad) are hard to tell apart. Of each sound's five nearest:

- 99.1% are of its kind, across the library;
- with the earlier six-band measure, 97.5%, toms the lowest kind at 80%;
- the suite requires 70% across the library and 50% for each kind.

Analysis takes about 1.4 ms a sound (0.15-4 s sounds, Apple silicon); the suite requires under 250 ms. It also checks determinism, silence, missing and non-audio files, and that a stereo copy of a kick finds the mono one nearest.

**A real library** (manual, `Halftrack --similarity-evaluate <folder> [per category] [report.json]`) treats each subfolder as a category. Against the reference's Core Library drum one-shots (Live 12 Suite 12.2.1, Samples/One Shots/Drums, read in place), the run used:

- 60 sounds per category, chosen by a fixed seed, in 17 categories: 929 sounds;
- 2.1 ms a sound.

Across the library, 40.6% of each sound's five nearest shared its category, where chance gives about 6%. The earlier six-band measure reached 36.4%.

| Category | Of the five nearest, same category |
| --- | --- |
| Kick | 71.7% |
| Ride | 61.7% |
| Clap | 57.7% |
| Cymbal | 55.0% |
| Shaker | 54.7% |
| Snare | 46.3% |
| Conga | 45.8% |
| Tom | 45.0% |
| Hihat | 39.0% |
| Rim | 35.7% |
| Wood | 30.9% |
| Bell, Tambourine | 25.0% |
| Bongo (14 sounds) | 21.4% |
| Misc Percussion | 20.0% |
| FX Hit | 19.7% |
| Electronic Percussion | 13.7% |

The mixed categories (Misc and Electronic Percussion, FX Hit) are the least coherent by nature. The report is kept in ignored artifacts; no sample or feature data from the reference is committed.

## Not yet covered

- The reference's own similarity model and results, which were not compared.
- Similar sample swapping in an instrument.
- Searching by similarity across collections and the User Library.
- A cache kept across launches.
- Sounds longer than ten seconds measured in full.
