"""Exact production C/JS rhythm parity; host only, no serial or release writes."""
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile
from host_tools import find_node, find_tcc

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / 'firmware/main/ui'
TCC = find_tcc()
NODE = find_node()


def compile_rules(source, target):
    subprocess.run([str(TCC), '-I' + str(SRC), str(source),
                    str(HERE / 'rules_test.c'), '-o', str(target)],
                   check=True, capture_output=True)


def main():
    if os.name == 'nt':
        # Expected assertion mutants must not open Windows crash-report dialogs.
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    with tempfile.TemporaryDirectory(prefix='nabo-duel-rules-', ignore_cleanup_errors=True) as directory:
        build = Path(directory)
        exe = build / 'rules.exe'
        compile_rules(SRC / 'duel_rules.c', exe)
        print(subprocess.check_output([str(exe)], text=True).strip())
        c = json.loads(subprocess.check_output([str(exe), '--trace'], text=True))
        js = json.loads(subprocess.check_output([NODE, str(HERE / 'trace.js')], text=True))
        assert len(c) == len(js), f'event counts differ: {len(c)} != {len(js)}'
        for i, (expected, actual) in enumerate(zip(js, c)):
            assert expected == actual, f'event {i}: JS={expected}, C={actual}'
        print(f'PASS C/JS exact parity: {len(c)} complete snapshots, 16 seeds, rounds 1..3')
        html = (ROOT / 'prototype/wireless-duel.html').read_text(encoding='utf-8')
        inline = re.search(r'<script>([\s\S]*?)</script>', html).group(1)
        script = build / 'prototype.js'
        script.write_text(inline, encoding='utf-8')
        subprocess.run([NODE, '--check', str(script)], check=True)
        print('PASS browser prototype JavaScript syntax')
        original = (SRC / 'duel_rules.c').read_text(encoding='utf-8')
        for label, old, new in [('perfect-window', 'err <= 60', 'err <= 59'),
                                ('outer-window', 'err = 181', 'err = 180'),
                                ('extra-penalty', 's->extras * 20', 's->extras * 10'),
                                ('skip-combo', 'if (best != s->last_index + 1) s->combo = 0;', '')]:
            assert old in original, f'mutation target missing: {old}'
            source = build / f'{label}.c'
            source.write_text(original.replace(old, new), encoding='utf-8')
            mutant = build / f'{label}.exe'
            compile_rules(source, mutant)
            result = subprocess.run([str(mutant)], capture_output=True)
            assert result.returncode != 0, f'{label} mutation escaped unit checks'
            print(f'PASS deliberate {label} regression rejected')
        print('HOST ONLY: no wireless, screen timing, audio latency or device acceptance claim')


if __name__ == '__main__':
    main()
