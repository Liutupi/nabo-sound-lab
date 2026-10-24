<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Echo voice quality checks

Run from the repository root with Python, NumPy, Node.js and a C compiler:

```sh
NV_UBSAN=1 python3 tests/voice_host/test_quality.py
```

The test generates 12 deterministic inputs: empty, quiet/normal/loud synthetic
syllables, a handling click, DC offset, silence, faint noise, short and odd-length
recordings, full-scale input and a recording beyond the three-second cap.
Each input exercises all 12 production voices and the actual prototype reference.
No private speech, downloaded fixtures or historical golden directory is needed.

Checks cover exact ADPCM/gain parity, playback duration and waveform agreement,
arbitrary recording/playback chunk boundaries, effect pitch/robot sidebands,
endpoint ramps and limiter
headroom. Independent quality checks require a useful digital level for quiet speech, bounded
loudness variation, resistance to one handling click, DC removal, rejection of a
6 kHz alias, preservation of the 3 kHz speech band and restrained radio tail hiss.
Fixed recording/player structure sizes are bounded. Generated inputs and reports
stay under ignored `out/voice-quality/`.

The recording path retains three seconds of 8 kHz IMA ADPCM. A DC blocker and
15-tap filter run before decimation; cubic reconstruction improves the former
linear playback interpolation. Gain follows the 80th percentile of 20 ms decoded
RMS windows above the very faint noise floor, capped at 32x. It no longer follows
the recording's single largest peak. Clips with no such window retain unity gain.
Per-effect trims and a soft knee above 0.75 full scale retain peak headroom.
Robot modulation includes dry voice instead of coarse bit crushing; radio filter
states retain fractional precision, and release hiss is quieter and follows the
recorded level. Exact silence produces no hiss. The record path and its stop/play
handoff share the audio mutex. Main volume, NVS formats, artwork and partitions
are unchanged; codec microphone gain is unchanged.

These tests establish DSP behavior, not intelligibility or perceived quality on
the badge. The 8 kHz recording bandwidth and small speaker remain constraints.
Device acceptance must compare ordinary speech at the same main volume, including
quiet syllables, pauses, loud words, voice changes and OK stop. Check background
noise, consonants, effect distinction, clipping, audio deadlines and user comfort.
Do not describe the gain cap or synthetic measurements as guaranteed acoustic gain.
