<p align="right"><a href="FLASHING_0_2_0_PREVIEW_2.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Install v0.2.0-preview.2

Target: ESP32-C3, 8 MB Flash. Internal version **0.2.0-dev** is expected.
Use attachments from this one release and verify `SHA256SUMS.txt` first.
The ZIP also includes `tools/verify_preview_release.py`; run it on the complete
downloaded attachment directory to check exact accepted binaries and ZIP contents:

```sh
python tools/verify_preview_release.py /path/to/downloaded-assets
```

## Preserve settings on an existing Sound Lab installation

Confirm that the currently running bootloader uses the Sound Lab layout below,
not merely that a residual partition table exists at a plausible address.
The table at `0x8000` must have SHA-256
`43311cbc95711332aa26b5e5002a58a9099b1a3be0b394408bc7b2b5dd986d86`.
Confirm the live port, chip identity and security configuration. The commands
below use esptool 4.x; replace `PORT` with the verified live port.

For preview.1 or another compatible installation with assets SHA-256
`5fc895c4ef5ef14a0aa90f40ca027d75806c40f4e7a44860f8538d1d5fa61edb`,
write **only the application**:

```sh
python -m esptool --chip esp32c3 --port PORT write_flash --flash_mode keep --flash_freq keep --flash_size keep 0x10000 nabo-soundlab-passport.bin
python -m esptool --chip esp32c3 --port PORT verify_flash 0x10000 nabo-soundlab-passport.bin
```

If the active layout matches but assets are older (for example stable v0.1.2),
also update `soundlab_assets.bin` at `0x200000`. Do not write the merged image at
`0x0` for a preserving upgrade. Neither path needs a whole-chip erase. Application
and optional assets writes leave NVS/PHY, the bootloader and partition table alone.

This release's app-only path was used on the user's badge. The protected
`0x0–0xFFFF` bytes were identical before/after flashing, including current settings
and game records. A full firmware readback is not a prerequisite for flashing.

## First installation or incompatible firmware

The complete file is **nabo-soundlab-0.2.0-preview.2-merged.bin**, written at **0x0**.
It replaces the previous application and overwrites stored data in its covered
range. Preserve any needed data separately. Do not apply these generic images to
a security-enabled device without its appropriate provisioning workflow.

```sh
python -m esptool --chip esp32c3 --port PORT flash_id
python -m esptool --chip esp32c3 --port PORT get_security_info
python -m esptool --chip esp32c3 --port PORT write_flash 0x0 nabo-soundlab-0.2.0-preview.2-merged.bin
```

The merged image contains blank NVS/PHY padding. Its bytes and component layout
are verified offline, but a single-file first installation was not tested on a
device for this revision. The tested preserving upgrade retained its existing
bootloader; the ZIP's bootloader comes from the matching new build.

| Component | Offset | Partition size |
| --- | --- | --- |
| Bootloader | 0x0 | Before partition table |
| Partition table | 0x8000 | 0xC00 image |
| NVS | 0x9000 | 0x6000 |
| PHY | 0xF000 | 0x1000 |
| Factory app | 0x10000 | 0x1F0000 |
| Assets | 0x200000 | 0x600000 |

After installation, test Echo recording, voice switching and OK stop at a
comfortable main volume. In volume editing, UP decreases and DOWN increases.
Read the [release notes](RELEASE_v0.2.0-preview.2.md) for validation and limits.
