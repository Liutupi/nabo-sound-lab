<p align="right"><a href="RELEASE_v0.2.0-preview.2.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# NABO Sound Lab v0.2.0-preview.2

Released 2026-10-24 for the FoloToy AI Passport, ESP32-C3 / 8 MB Flash.
This preview contains the same wireless application accepted on the user's badge.
The internal version remains **0.2.0-dev**; the release packages frozen binaries
without rebuilding them to change a version string. Stable v0.1.2 remains available.

## Changes since preview.1

- Button hints highlight immediately. Echo controls reflect recording/playback;
  OK stops playback without starting a new recording on release.
- In settings, OK on volume enters editing; UP decreases, DOWN increases,
  0–100% limits stop, and OK finishes. Double-UP leaves editing before leaving settings.
- Echo voice selection works during recording and playback. A new voice replays
  the retained recording; switching manual/automatic mode cancels transient audio.
- Quiet echo speech receives stronger, sustained-level normalization. Recording
  removes DC and filters before decimation; playback uses cubic reconstruction,
  effect level trims and a soft limiter. Robot modulation keeps more clear voice;
  radio hiss is quieter. Main volume, persisted settings, partitions and NABO art
  are unchanged from preview.1.
- Pitch analysis rejects ambiguous noisy subharmonic candidates. This is not a
  guarantee of professional tuning accuracy in every acoustic environment.

## Validation

```text
Build: PASS — ordinary and wireless ESP-IDF 5.5.3 builds and archive verification
Host tests: PASS — 12 inputs x 12 voices, 14 UI/audio states, prototype controls,
                   48 pitch signals / 1625 frames; GitHub CI passed
Device tests: PASS — the packaged wireless app was flashed and hash-verified;
                     original settings/records survived; 12 voices were swept;
                     the user accepted the current listening result for release
Unverified: full merged-image installation; this revision's two-badge RF/range
            regression; power-cycle testing; broader speaker/listening conditions
```

The device mixer peak was 3.245 ms for 20 ms blocks, with zero audio read/write
errors in the controlled sweep. This measures the mixer, not all task latency.
Complex echo animations still had slow frames, up to 65 ms in that sweep. The
8 kHz ADPCM bandwidth and small speaker remain limits. Synthetic quiet-syllable
gain improved about 13.7 dB; this is not a promised acoustic gain.

Earlier preview.1 two-device results remain historical evidence and were not
repeated for this revision. The new merged image was checked component-by-component
and for blank padding offline; only its app was written during the preserving
upgrade. See the [sanitized validation](VALIDATION_0_2_0_PREVIEW_2.json).

## Download and use

[Release assets](https://github.com/Liutupi/nabo-sound-lab/releases/tag/v0.2.0-preview.2)
include a complete ZIP, separate components, merged image, manifest and checksums.
Follow the [installation guide](FLASHING_0_2_0_PREVIEW_2.md), especially the
application-only upgrade for a compatible preview.1 installation.

On Echo, hold OK to speak and release to replay. UP changes voice, DOWN switches
manual/automatic mode, OK stops playback, and double-UP returns to the menu.
The wireless duel remains a three-round offline ESP-NOW game; both badges should
use the same release. No internet, router or account is required.

Build using the pinned dependencies and ESP-IDF 5.5.3 as described in
[BUILD](BUILD.md); the wireless entry is `firmware/tools/build-duel.ps1 -Action all`.
Local rebuilds need their own validation and do not inherit these exact hashes.
The host voice checks are [documented here](../tests/voice_host/README.md).

Application SHA-256: `fbbf94fd3cf1fd096124c98b1611b4c55123725a0e7e7c6df6d5305ddc5612ac`.
Merged SHA-256: `2e5f71d459980abd6c2a64bff0b74ba830bcff591a45b2dd32fea51d6c037c51`.
The build source is `555125b7b9a6398fa5293c147cb2d3c4e8e05f80`; the release tag
adds documentation and release verification without changing firmware sources.

Source licensing does not grant rights to the NABO artwork; see the repository
NOTICE and THIRD_PARTY_NOTICES. This project is not affiliated with FoloToy.
