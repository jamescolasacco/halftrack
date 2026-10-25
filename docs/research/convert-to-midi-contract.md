# Convert to MIDI contract

Status: partial. The commands follow the reference's manual; the analysis is Halftrack's own. No reference output was measured.

## Evidence

- Live 12 manual, [Converting Audio to MIDI](https://www.ableton.com/en/live-manual/12/converting-audio-to-midi/), consulted 2026-10-25:
  - With an audio clip selected, the conversion commands are in the Create menu and the clip's context menu (chapter 13).
  - Convert Harmony to New MIDI Track identifies the pitches in polyphonic audio and places them in a clip on a new MIDI track, which plays a piano sound from an Instrument Rack (13.2). Convert Melody does the same for monophonic audio, with a synthesizer whose "Synth to Piano" macro moves it toward an electric piano (13.3). Convert Drums extracts the rhythm of unpitched, percussive audio, identifying kick, snare and hi-hat so they play the matching sounds of the track's Drum Rack (13.4).
  - The converted clip does not play the original sound. The transient markers in the original clip divide its notes, so adding, moving or deleting them tunes the result (13.5).
- The reference's Create menu puts Convert Harmony, Melody and Drums to New MIDI Track after Slice to New MIDI Track, and its clip menu offers them with Slice after a separator for exactly one selected audio clip (static reading; see the [menu bar contract](main-menu-contract.md) and the clip menu research).

## Halftrack's behaviour

- **The commands.** Create > Convert Harmony, Melody or Drums to New MIDI Track, and the same items after Slice to New MIDI Track in an audio clip's menu (`Session::convertToNewMidiTrack`). Each makes a MIDI track right after the clip's track with a MIDI clip in the audio clip's place and of its length, holding the notes found, selects it, and is one undo step named for the command.
- **The sound.** Harmony and Melody tracks play Halftrack's synthesizer. A Drums track plays a Drum Rack of Halftrack's own kick, snare and closed hi-hat on C1, D1 and F#1, generated and written to the project's media (`Halftrack Kick.wav` and so on). The reference's piano, synthesizer and drum kits are not used.
- **The clip.** An unlooped, unreversed Arrangement audio clip, unwarped or warped without markers, as slicing takes; Session clips are not converted. Audio with no notes found is refused, and nothing is added.
- **The analysis** (`src/AudioToMidi.h`), Halftrack's own:
  - The clip's audio, mixed to mono, is cut at the transients Halftrack's warping finds. Transients closer than 50 ms are one onset, and one where the sound grows by less than 40% over 20 ms is none (tones held together beat). Audio sounding before the first transient makes a part from its start.
  - Each note starts at its part's onset. A melody's or chord's note lasts until the part's sound falls 30 dB below its attack; a drum's lasts at most 100 ms. Velocity runs from a fifth for the quietest onset to full for the loudest.
  - Melody: one pitch per part, by YIN over up to 4096 frames from just after the attack, as the Tuner measures pitch.
  - Harmony: up to six pitches per part from a 16384-point spectrum of its steadier middle: the pitch whose eight harmonics (weighted 1/√h) sum highest, with its own fundamental sounding, then the next after its harmonics are taken out, while each is at least 15% of the first. MIDI notes 28 to 100.
  - Drums: what each onset's first 40 ms adds to the spectrum over the 40 ms before it: a kick when most lies below 150 Hz, a hi-hat when over 40% lies above 6 kHz with almost none below 150 Hz, otherwise a snare.

## Not followed

- The reference's analysis, its instruments and their macros, and conversion of Session clips or clips with warp markers. The reference's results on the same audio were not compared.

See [open questions](../open-questions.md#convert-to-midi) for the defaults taken.

## Evidence in Halftrack

The convert-midi suite generates audio and checks that four harmonic tones half a second apart become their four notes at their onsets, lasting about as long; that a C major and then an A minor chord become their three notes each; that Halftrack's kick, hi-hat, snare and hi-hat become C1, F#1, D1 and F#1 at their onsets; and that silence has no notes. Through the Session, Convert Melody puts a MIDI track after the clip's track with a clip in its place holding the melody's notes at their beats, one undo step that undo reverses; Convert Drums makes a Drum Rack track with pads on C1, D1 and F#1 playing Halftrack's sounds from the project's media, its clip holding the four hits; an unknown clip and audio without notes are refused, the latter adding no track. The UI suite lists the commands after Slice in an audio clip's menu and the Create menu, and checks that Convert Drums makes and selects a Drum Rack track in one undo step.
