<p align="right"><a href="BUILD.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Build and assets

The wireless v0.2.0-preview.2 release uses frozen 0.2.0-dev binaries. Its build
entry is `firmware/tools/build-duel.ps1 -Action all`, with separate build-duel and
sdkconfig.duel paths. Tracked defaults describe the build configuration; release
binaries remain frozen.
Ordinary build.ps1 omits the wireless menu. Set dependency/tool paths for your
own environment. See [duel host dependencies](../tests/duel_host/README.md).
A local rebuild requires its own validation and does not inherit the frozen
Release's device acceptance.

The stable release is frozen v0.1.2 r4; it was not rebuilt for publication.
Toolchain, build time and configuration can change a local build's SHA.

Dependencies: ESP-IDF 5.5.3, ESP32-C3 toolchain, button 4.2.0 and esp_codec_dev
1.6.2, as listed in firmware/components/bsp/idf_component.yml. Windows
firmware/tools/build.ps1 accepts `-IdfPath`, `-VendorDir` and `-ToolsPath`.
Its default paths come from the author's cache; replace them for your environment.
VendorDir needs the matching button, esp_codec_dev and transitive components.

The repository includes firmware/assets/soundlab_assets.bin and all runtime
assets. Building frozen assets does not require regenerating artwork or fonts.
Asset and release SHA values are in the validation JSON and Release manifest.
Regeneration requires Python, Pillow, fontTools and NumPy; video frame extraction
also requires FFmpeg.

```powershell
python -m pip install Pillow fonttools numpy
# Only after intentionally changing assets/glyphs; these alter generated outputs:
python tools/gen_fonts_soundlab.py
python tools/pack_props_soundlab.py
python tools/pack_assets_soundlab.py
& .\firmware\tools\build.ps1 -Action all -IdfPath <idf-directory> -VendorDir <component-directory> -ToolsPath <IDF-tools-directory>
```

Host tests live under tests/. Some older Windows entries retain local TCC/Node
paths and need adjustment; tools and caches are not distributed. Synthetic
signals come from tests/mic_host/gen_signals.py; environmental recordings are not
included. New self-contained checks are documented under
[interaction tests](../tests/interaction_host/README.md),
[microphone tests](../tests/mic_host/README.md), and
[voice quality tests](../tests/voice_host/README.md).

Serve the repository with `python -m http.server 8000` and open
prototype/index.html. Browser microphone use requires permission. The prototype
is not a replacement for flashed-device acceptance. Its volume controls now
match the firmware; its older placeholder artwork is not current firmware art
acceptance evidence.

Final NABO artwork is game-art/soundlab/costume/final.png and props/; sprites/
contains the indexed runtime assets. Source videos remain available for animation
review. Generation job records, signed URLs, backups and device logs are private.
