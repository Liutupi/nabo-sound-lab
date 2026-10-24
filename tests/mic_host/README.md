<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Microphone DSP host checks

Run the pitch regression suite from the repository root with Python 3.10+,
NumPy, Node.js and a native C compiler (GCC or Clang):

```sh
python -m pip install numpy
python tests/mic_host/test_pitch.py
```

The runner uses `CC` (default `cc`) and `NABO_NODE` (default `node` on `PATH`).
It compiles the production DSP into a temporary shared library and removes its
test outputs on exit. On Linux with GCC, enable undefined-behavior checking with:

```sh
NM_UBSAN=1 python tests/mic_host/test_pitch.py
```

The 48 deterministic synthetic signals cover 60.5–1046.5 Hz pure tones,
harmonic-rich tones, weak fundamentals, noisy A4, silence, DC, white noise,
near-full-scale input and note/silence transitions. Checks measure accuracy and
coverage against the generated frequency, then compare 1,625 pitch frames with
the actual `yin()` function extracted from the browser prototype. Browser and
microphone access are unnecessary. A deliberate removal of the ambiguity guard
must fail the noisy A4 accuracy check.

## Noisy subharmonic rejection

The fixed 8 dB SNR A4 fixture previously returned a false low pitch in 6 of 73
steady detection frames, including approximately 147 Hz for a 440 Hz input.
The change rejects a candidate when an earlier local CMND minimum is within
`1966/65536` (approximately 0.03) of the accepted minimum and lies at an integer
submultiple of its lag, allowing one sample of rounding per shorter period.
Both pitch and confidence become zero for the ambiguous frame. This avoids
guessing between a noisy short period and a harmonic-rich true low note.

The fixture now retains 67 of 73 steady detections near 440 Hz and reports no
false low pitches. This is a scoped synthetic regression result, not a general
noise immunity guarantee. The extra scan reuses the existing CMND array; it adds
no allocation, state, floating-point DSP or sample-difference calculations.
Firmware and prototype use the same rejection rule. Existing UI hold times
(including the tuner's 600 ms hold) remain unchanged.

This is an unreleased source change. It does not alter the frozen v0.1.2 or
v0.2.0-preview.1 release binaries or their recorded device acceptance. Actual
microphone noise, C3 execution time, physical controls and listening still need
on-device checks. No test here opens a serial port or flashes firmware.

## Full DSP parity suite

The older suite also checks levels, spectrum, waterfall, onsets, BPM and chords.
Generate its synthetic signals and browser reference before running it:

```sh
python tests/mic_host/gen_signals.py
node tests/mic_host/record_golden.js prototype/index.html tests/mic_host/signals tests/mic_host/golden
python tests/mic_host/test_nabo_mic.py
```

Reference generation additionally needs Playwright and Chromium. Set `PW_MODULE`
to the Playwright package path and `PW_CHROME` to an installed Chromium executable
if necessary. The full suite now fails explicitly when no signal fixtures exist;
an empty fixture directory is never a passing test. Generated signals, browser
references and shared libraries stay out of Git.
