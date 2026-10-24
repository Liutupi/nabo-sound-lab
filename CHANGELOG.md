<p align="right"><a href="CHANGELOG.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Changelog

## Unreleased

## v0.2.0-preview.2 - 2026-10-24

- Add immediate button feedback, state-specific Echo hints and OK playback stop without accidental recording.
- Add volume editing with UP decrease, DOWN increase, clamped limits and saved settings.
- Keep voice selection and manual/automatic switching available during recording and playback.
- Improve quiet echo loudness, recording filtering, reconstruction, soft limiting and effect balance; make robot voice clearer and radio tail hiss quieter.
- Reject ambiguous noisy subharmonic pitch candidates.
- Preserve NABO art, preview.1 assets, partitions and stored-data formats. Publish the frozen application accepted by the user, with separate full-image and preserving-upgrade instructions.
- Build/host checks and one-badge app/selection/stop/sound acceptance passed. Full merged installation and this revision's two-device RF/range remain unverified; retain preview status. Complex Echo animations still have slow frames.

See [release notes](docs/RELEASE_v0.2.0-preview.2.md) and [validation](docs/VALIDATION_0_2_0_PREVIEW_2.json). Application SHA-256: `fbbf94fd3cf1fd096124c98b1611b4c55123725a0e7e7c6df6d5305ddc5612ac`.

## v0.2.0-preview.1 - 2026-10-24

- Add two-badge ESP-NOW rhythm duels: four-digit rooms, explicit acceptance, mutual readiness, three rounds, mute support and disconnect invalidation.
- Reuse the NABO musician and actions, complete Chinese glyphs, and use 16px primary controls / 22px scores.
- Fix missing 120px sprites after wireless heap fragmentation; check chorus and older games.
- Validate 12 matches / 36 rounds on two devices, including 10 consecutive matches, rotating hosts and hard disconnect handling, at about30.3 FPS.
- Publish the tested internal0.2.0-dev images without rebuilding. Physical feel, audiovisual timing, range and full merged-image installation remained unverified.

Application SHA-256: `5d8440c6494d2464b379c49f0af31e12e658e4e2c80ace6e058325c1308c4d24`. Assets SHA-256: `5fc895c4ef5ef14a0aa90f40ca027d75806c40f4e7a44860f8538d1d5fa61edb`. The release manifest identifies the other components and attachments.

## v0.1.2 - 2026-10-23

- First public release with the NABO musician, 38 source animation clips, runtime sprites, props and static UI assets.
- Home greeting reuses the approved young-boy voice and PCM from the fortune-machine application.
- Add humming flight, clap rhythm, sampler piano and saved0–100% main volume with true mute at0%.
- Keep primary Chinese hints at16px and settings values at22px; fix action anchors/playback mappings and initialize voice tables before first playback.
- Application510160B, remaining app partition1521456B; assets1882628B, remaining asset partition4408828B.
- Application SHA-256: `fa93210ee93d5e569289c8ed4e87615ca339b3e63266678e0acb5f2480f39869`.
- Assets SHA-256: `37018a9263a5110af7ed8b38ce2f64ed51ba004ae4d7d6327b6eb1bc7e916604`.

See the historical validation under `docs/VALIDATION_v0.1.2.md` for noisy-pitch, unexplained reset and voice timing/listening limits. Internal0.1.0/0.1.1 were not public releases; the actual firmware version remains0.1.2.
