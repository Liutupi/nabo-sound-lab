# Startup PCM and sampler host integration

Run from the repository root with a Python 3 standard library runtime:

```powershell
& 'C:\Users\pc\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' tests/boot_voice_host/run_tests.py
```

The runner uses the existing portable Win64 TCC under the 0.1.1 offline host-tools directory. It compiles unchanged production `sl_audio.c`, `sl_sampler.c`, `fc_res.c` and `nabo_voice.c`. Thin include wrappers add exports and test allocator counters. The platform lock counts lock depth; it does not emulate FreeRTOS scheduling.

It imports the actual `tools/pack_assets_soundlab.py`, changes only its `OUT` variable to `dist/soundlab-0.1.2/offline-review/audio/isolated-pack/`, then calls `main()`. No production output is written. The real FNAS reader provides the boot PCM pointer to the actual mixer.

Tests cover full WAV payload equality and one-shot PCM output, natural completion, stop/restart, malformed/missing PCM, exact ducking and restoration at the final sample, saturation using an explicitly synthetic extreme fixture, all three real sampler pitches against an independent Python fixed-point reference, sampler coexistence with boot voice, sampler allocation/release, existing ADPCM playback, and the exact extracted BOOT lifecycle functions. The lifecycle test exercises source control flow; it does not run the full UI or hardware scheduler.

Results, source/compiler/test hashes, build commands, isolated assets and test binaries go only under `dist/soundlab-0.1.2/offline-review/audio/`. This is not a final release verification or a physical listening test. No serial device is opened.
