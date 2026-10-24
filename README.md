<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# NABO Sound Lab

Put on your headphones and play music with NABO. An independent offline firmware
for the FoloToy AI Passport badge: ESP32-C3, 8 MB Flash, 240×320 display and ES8311.

**Wireless duel preview: v0.2.0-preview.1.** Two badges connect directly for three
rhythm rounds, without a router, internet or account. The tested internal version
is 0.2.0-dev. Physical key feel, audiovisual synchronization and range remain
unverified. Single-player use is offline; duels temporarily enable ESP-NOW.
The firmware does not include Xiaozhi chat.

[Download duel preview](https://github.com/Liutupi/nabo-sound-lab/releases/tag/v0.2.0-preview.1) · [Preview installation](docs/FLASHING_0_2_0_PREVIEW.md) · [Release notes](docs/RELEASE_v0.2.0-preview.1.md) · [Stable v0.1.2](https://github.com/Liutupi/nabo-sound-lab/releases/tag/v0.1.2) · [Build guide](docs/BUILD.md)

**Unreleased source improvements:** common button hints highlight immediately;
echo controls follow the recording/playback state and OK stops playback; volume
supports UP to decrease and DOWN to increase, stopping at 0% and 100%. Existing
NABO artwork and colors are preserved. See [controls and tests](tests/interaction_host/README.md)
and [pitch ambiguity rejection](tests/mic_host/README.md). Echo playback also adds
speech-level normalization, filtered recording, clearer robot modulation and quieter
radio hiss; see [voice quality checks](tests/voice_host/README.md). These source changes
are not included in the frozen Release images below and require separate device
acceptance.

The development menu includes wireless duels: both badges use the same chart,
with four count-in beats and three rounds of OK taps. Singing and pitch detection
are unnecessary; muted play works. [Interactive two-screen prototype](prototype/wireless-duel.html) · [Rules and development acceptance](docs/WIRELESS_DUEL.md).
The 0.2.0-dev and stable v0.1.2 identities remain separate; see the
[development validation record](docs/VALIDATION_0_2_0_DEV.json).

On 2026-10-24, two real C3 badges completed 12 controlled matches and 36 rounds,
including 10 consecutive matches, host rotation and muted scoring, at approximately
30 FPS. A sudden disconnect correctly invalidated the match.
[Two-device overview](docs/images/duel-device-real-rf-overview.png).
Physical key feel, audio synchronization and range remain pending; v0.1.2 is still
the stable release.

![NABO musician](docs/images/nabo-musician.png)

## What can you play?

| Group | Features |
| --- | --- |
| Wireless duel preview | Four-digit rooms, beat-following OK taps, three rounds, optional sound, disconnect invalidation |
| Sound toys | 12 echo voices, voice-controlled running, candle blowing, tightrope singing, vocal range card, humming flight, clap rhythm, sampler piano |
| Sound visualization | Spectrum, waterfall, oscilloscope, chord circle, sound-level display |
| Music practice | Tuner, guitar toolbox, beat detection, pitch trace |
| Settings | Volume, A4 reference, reduced flashing, quiet threshold, dB calibration, spectrum sensitivity |

The home greeting uses the same sunny young-boy voice as the NABO fortune machine:
NABO invites you to play music. Volume is 0–100% in 10% steps, default 80%, saved
automatically; 0% is mute. UP/DOWN select and OK acts; double-UP returns. Each page
shows its current controls. Sampler piano uses all three keys for notes; hold OK
to record. In current source, OK on the volume row enters adjustment, UP decreases,
DOWN increases and OK finishes; double-UP leaves adjustment before the settings page.

The finalized NABO musician and props are retained. Main Chinese instructions use
16 px text and setting values use 22 px. After approximately five minutes of
inactivity the badge sleeps; the physical OK key wakes it.

## Firmware downloads and installation

Download the complete v0.2.0-preview.1 ZIP and follow the
[preview installation guide](docs/FLASHING_0_2_0_PREVIEW.md). Seeing internal version
0.2.0-dev is expected. Existing Sound Lab installations with the same layout can
upgrade app and assets; other applications require the complete matching layout
and appropriate handling of their stored data. The preview has controlled real
two-device evidence; physical feel, audiovisual synchronization, range and a
single merged-image installation remain unverified.

### Stable v0.1.2

For first installation, flash **nabo-soundlab-0.1.2-merged.bin** at 0x0, selecting
ESP32-C3 and 8 MB Flash. This replaces the previous application; preserve any
needed data first. With the same Sound Lab 0.1.1/0.1.2 partition layout, follow the
installation instructions to upgrade app/assets while retaining settings. Do not
reuse addresses from other applications.

The Release also includes a complete ZIP, separate images, manifest.json and
SHA256SUMS.txt. These are frozen verified binaries, not rebuilt for publication.
Device validation covers the r4 app and assets with the existing bootloader;
the complete package contains the matching build's newer bootloader. First-time
installation of that complete package has not received a separate end-to-end
device test.

## Stable v0.1.2 validation scope

Volume 0/40/80% survived three controlled chip resets. All 12 voices completed
recording/playback with zero audio API read/write errors; the first recording
processing peak was 0.680 ms. Four formal performance windows reached 30.30 FPS
without slow frames; actual voice performances still had slow frames.

Open items: noisy 440 Hz input was once identified near 147 Hz; an older revision
unexpectedly returned/rebooted while idle, with the cause still unknown and no
recurrence in subsequent observation; subjective popping in the newer voice
revision still needs listening checks. The firmware does not promise professional
tuning or sound-level accuracy. See the [validation record](docs/VALIDATION_v0.1.2.md)
for evidence and limits; later source changes do not rewrite that historical scope.

![Nine settings states from the C3 display buffer](docs/images/settings-overview.png)

These are actual 240×320 C3 display buffers, not optical panel photographs. The
contact sheet background is #0A0716; frames were captured at different times.

## Source layout

| Directory | Contents |
| --- | --- |
| firmware/ | Application, BSP, configuration and frozen asset pack |
| prototype/ | Browser prototypes; volume controls are synchronized, older placeholder artwork is not firmware artwork acceptance |
| game-art/soundlab/ | Final character, props, starting images, 38 source videos, runtime sprites and fonts |
| tools/, tests/ | Asset tools and host-test sources |
| docs/ | Installation, builds, validation, releases and native-resolution screenshots |

Local device backups, NVS, environmental recordings, raw device logs, generation
job metadata and caches are not uploaded. Source code is MIT; NABO artwork is not
licensed under MIT. See [NOTICE](NOTICE.md) and [third-party notices](THIRD_PARTY_NOTICES.md).
This project is not affiliated with FoloToy.

## Community presentation

[Five promotional images and gameplay introduction](docs/community/v0.1.2/README.md).
The application was submitted to the AI Passport community; the presentation
notes describe submission status and firmware validation scope.
