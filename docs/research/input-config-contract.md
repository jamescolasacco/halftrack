# Input configuration contract

Halftrack's input configuration follows the reference's Input Config dialog. It decides how the audio device's input channels appear as track inputs.

## Behaviour

- **Pairs.** Each pair of input channels, starting on an even channel, is one stereo input or two mono inputs.
  - Mono inputs record one-channel takes of their own channel.
  - Asking for a pair on an odd channel, a channel the device lacks, or a stereo pair with no second channel is refused.
- **On and off.** Each input can be switched off and on.
- **Names.** Inputs are named by their channels (for example "Generated left" and "Generated right" for the two mono halves of the test device's pair). A track keeps an input that still exists after a change.
- **Persistence.** The layout is kept with the device settings, so the next Session opens with it. Changes are refused while recording.
- **The UI.** The Input Config menu on Preferences' Audio page offers Stereo or Two Mono for each pair and an on/off switch for each input.

## Evidence

`tests/InputConfigTests.cpp` uses the synthetic device, whose left input plays 440 Hz and right 660 Hz:

- **The pair.** It starts as one stereo input and splits into two mono inputs.
- **Recording.** Two tracks record them at once; each take is one channel at 440 Hz and 660 Hz.
- **Refusals:** odd and missing channels, and an unknown input.
- **Switching.** The right input switches off and on.
- **Persistence.** The next Session opens with the mono inputs, and the pair joins again.

The UI suite checks the panel lays out its Input Config and MPE Inputs buttons.

## Not yet covered

- The reference can offer a pair as a stereo input and two mono inputs at once. Here a pair is one or the other.
- Renaming inputs is missing. Outputs have their own Output Config ([output routing contract](output-routing-contract.md)).
- Real interfaces with more than two channels are untested.
