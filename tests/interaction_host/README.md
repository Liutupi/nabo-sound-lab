<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Button, playback and volume interaction checks

This source update preserves the existing musician artwork, palette, assets
partition, audio timbres and NVS format. It does not replace any frozen release
binary or its device-validation record.

## Controls

- Common button hints highlight immediately on press and remain visible for at
  least 160 ms. UP stays highlighted while its existing 300 ms double-click
  window is pending. The action still waits for that window, so returning with
  double-UP does not first change a setting or start an audio action. Feedback
  adds no sound, input delay or blocking work. Pages with their own custom
  footer retain their existing feedback.
- On the volume row, OK enters adjustment; UP decreases and DOWN increases by
  10 percentage points. Values stop at 0% and 100%, without wrapping. OK finishes.
  Double-UP returns to the settings list, then double-UP again returns to the
  menu. Each actual change uses the existing asynchronous codec application and
  delayed NVS save; returning keeps the chosen value.
- Echo shows record hints only when idle, release-OK while recording manually,
  and stop-OK while playing in either manual or automatic mode. OK during playback
  stops using the existing audio fade. Releasing that press does not start or
  stop a new recording; the next fresh press can record in manual mode.
- Voice and recording mode can change only while idle, so the displayed voice
  and animation remain consistent with the active audio. Automatic mode retains
  its cooldown after playback/stop, and leaving stops recording/playback.
- The browser prototype demonstrates the same controls and persists its volume
  locally. Its older placeholder artwork remains a prototype limitation; it is
  not a current firmware artwork or physical-audio acceptance reference.

## Run

Requirements: Python 3.10+, NumPy, Pillow and a native GCC/Clang C compiler.
`CC` selects the compiler, defaulting to `cc`.

```sh
python -m pip install numpy Pillow
python tests/interaction_host/run_tests.py
NABO_UBSAN=1 python tests/interaction_host/run_tests.py
node tests/interaction_host/test_prototype.js
```

The last command additionally requires Node.js. These tests use the real C
handlers, audio/voice processing, renderer and frozen asset pack. Host-only hooks
observe text, glyph coverage, visible text bounds, setting-save requests and
codec-volume handoffs. Shared libraries and renamed rendering boundaries are
temporary; snapshots go to ignored `out/interaction-host/`.

Coverage includes immediate feedback and expiry, duplicate press suppression,
uint32 clock wrap, single/double-UP separation, both volume limits, save/apply
hooks, manual and automatic recording/playback, stopping without accidentally
recording, natural completion, active voice/mode locking and return teardown.
Every captured frame checks missing glyphs, visible text bounds, display-list
capacity and incremental/full pixel equality. Tests do not open serial ports or
flash a device. Physical buttons, codec volume, Chinese panel rendering and
listening still require on-device acceptance.
