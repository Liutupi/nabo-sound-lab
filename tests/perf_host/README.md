# Renderer/cache host regression tests

Run from any directory with Python and NumPy available:

```text
python tests/perf_host/run_perf_tests.py
```

The Windows default uses the previously downloaded portable TCC from the frozen
0.1.1 offline review. Select another host compiler explicitly if needed:

```text
python tests/perf_host/run_perf_tests.py --cc /usr/bin/gcc
```

The tests read the exact accepted 0.1.1 `soundlab_assets.bin`, independently
decode real FNBO delta/RLE frames in Python, and compile two host-only libraries
into `out/`. The `baseline/` files preserve the renderer/decoder immediately
before this performance change. They are a pixel and allocation reference, not
production sources or an executed ESP32 binary.

Coverage:

- Forward playback, repeated frame-zero wrap and reverse/random frame seeks.
  A single 120 px animation must retain one 14400-byte allocation.
- Real chorus: one 120 px and two distinct 76 px frames in the same epoch,
  25952 bytes total; all returned pointers remain valid after later requests.
- Four pinned image buffers totaling 31728 bytes survive an intentional
  over-budget request. That request returns NULL without corrupting prior frames;
  the next epoch can reuse/evict old buffers.
- 110 complete chorus frames match the frozen renderer pixel-for-pixel and each
  incremental render equals forced full render.
- 160 four-layer waveform cases (sine, dense noise, impulses, near-flat) compare
  old callbacks with new column spans under offsets and clipping. Unchanged
  frames must skip every dirty strip.
- The new span operation must not enlarge `op_t` or the display-list storage.
- A repeated same-compiler host benchmark isolates callbacks versus spans.

`out/performance-host.json` records source/compiler/asset hashes, exact build
commands, allocation counts, comparisons and timings. TCC has no ASan/UBSan;
these tests do not replace the ESP-IDF build or real-device SPI/frame/heap tests.
They never open a serial port, repackage assets or modify the old dist binaries.
