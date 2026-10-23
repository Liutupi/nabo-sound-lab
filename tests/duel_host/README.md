# Wireless duel host checks

These checks compile the production C sources on the host. They do not open a
serial port, flash a badge, or establish physical key, audio, display or RF
acceptance. Test reports distinguish simulation from real-device evidence.

The tested host environment is Windows, Python 3.10 or newer, native TinyCC,
Node.js, Pillow and NumPy. The UI contact sheets currently use the standard
Windows Consolas font at `C:/Windows/Fonts/consola.ttf`; running those presentation
steps on another platform needs an available font path. No compiler, Node runtime
or private `dist/` directory is included in the repository.

Install TinyCC and Node.js and add `tcc` and `node` to `PATH`. Alternatively, set
the executable paths for your installation:

```powershell
$env:NABO_TCC = 'C:\Tools\tcc\tcc.exe'
$env:NABO_NODE = 'C:\Program Files\nodejs\node.exe'
python -m pip install Pillow numpy
```

TinyCC lookup uses `NABO_TCC`, then `PATH`, then the optional old local
`dist/soundlab-0.1.1/offline-review/host-tools/tcc/tcc.exe` when present. An explicit
`--cc` (`test_fragmented_cache.py`) or `--compiler` (`radio_fault_test.py`) takes
precedence. Node lookup uses `NABO_NODE`, then `PATH`. An invalid explicit setting
fails with an installation hint instead of silently choosing another executable.

From the repository root:

```powershell
python tests/duel_host/test_rules.py
python tests/duel_host/test_network.py
python tests/duel_host/radio_fault_test.py
python tests/duel_host/test_ui.py
python tests/duel_host/test_fragmented_cache.py
python tests/duel_host/test_legacy_ui.py
```

The rules and network checks exercise scoring and packet state transitions with
deliberate regression mutations. The radio check uses a fake SDK. UI and cache
checks compare actual C renderer pixels and constrained allocations. Outputs may
update `network-validation.json`, `ui-validation.json`, `docs/images/` host renders,
or ignored `device-validation/` host reports. None of these commands runs the
private serial-controlled hardware test scripts. A fresh local build or host test
run does not replace the frozen firmware already verified on the two badges.
