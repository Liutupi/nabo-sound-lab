"""Host test tool discovery. No device access or executable downloads."""
from pathlib import Path
import os
import shutil

ROOT = Path(__file__).resolve().parents[2]
LEGACY_TCC = ROOT / 'dist/soundlab-0.1.1/offline-review/host-tools/tcc/tcc.exe'


def _tool_file(value, label):
    candidate = Path(value).expanduser()
    if candidate.is_file():
        return candidate.resolve()
    found = shutil.which(str(value))
    if found:
        return Path(found).resolve()
    raise RuntimeError(f'{label} does not identify an existing executable: {value}. '
                       'See tests/duel_host/README.md for installation and environment settings.')


def find_tcc(override=None):
    """Explicit CLI path, then NABO_TCC, PATH tcc, optional old local copy."""
    if override is not None:
        return _tool_file(override, 'Explicit TinyCC path')
    configured = os.environ.get('NABO_TCC')
    if configured:
        return _tool_file(configured, 'NABO_TCC')
    found = shutil.which('tcc')
    if found:
        return Path(found).resolve()
    if LEGACY_TCC.is_file():
        return LEGACY_TCC.resolve()
    raise RuntimeError('TinyCC was not found. Install a native TinyCC C compiler and add tcc to PATH, '
                       'or set NABO_TCC to its executable path. The optional dist/ compiler is not '
                       'distributed by this repository. See tests/duel_host/README.md.')


def find_node():
    """NABO_NODE, then PATH node; never an author's machine-specific path."""
    configured = os.environ.get('NABO_NODE')
    if configured:
        return _tool_file(configured, 'NABO_NODE')
    found = shutil.which('node')
    if found:
        return Path(found).resolve()
    raise RuntimeError('Node.js was not found. Install Node.js and add node to PATH, or set NABO_NODE '
                       'to its executable path. See tests/duel_host/README.md.')
