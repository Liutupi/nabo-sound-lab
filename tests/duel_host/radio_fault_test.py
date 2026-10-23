"""Compile the unchanged production radio with a fault-injectable host SDK.

This validates lifecycle/queue logic, not ESP32 allocation, RF or timing. All
generated SDK headers and executables live in a temporary directory. Three
intentional production-code mutations must fail the same fixture.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from host_tools import find_tcc

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, help='TinyCC executable (overrides NABO_TCC/PATH discovery)')
    parser.add_argument('--out', type=Path, default=ROOT / 'device-validation/duel-0.2.0/radio-fault-validation.json')
    args = parser.parse_args()
    args.compiler = find_tcc(args.compiler)
    production = ROOT / 'firmware/main/link/sd_radio.c'
    api = production.with_suffix('.h')
    fixture = Path(__file__).with_name('radio_fault_test_fixture.c')
    mock = Path(__file__).with_name('radio_fault_test_mock.h')
    if not args.compiler.is_file():
        parser.error('Pass --compiler with a working TinyCC C compiler')
    checks = {}
    with tempfile.TemporaryDirectory(prefix='soundlab-radio-test-') as temporary:
        work = Path(temporary)
        (work / 'freertos').mkdir()
        (work / 'mock.h').write_bytes(mock.read_bytes())
        for name in ('esp_err.h', 'esp_event.h', 'esp_mac.h', 'esp_now.h', 'esp_timer.h', 'esp_wifi.h', 'freertos/FreeRTOS.h'):
            (work / name).write_text('#include "mock.h"\n', encoding='utf-8')

        def compile_and_run(name, source, wireless=True):
            target = work / (name + '.exe')
            command = [str(args.compiler), '-I' + str(work), '-I' + str(api.parent), '-o', str(target)]
            command += ['-DSOUNDLAB_WIRELESS_DUEL=1'] if wireless else ['-DTEST_STUB=1']
            subprocess.run(command + [str(fixture), str(source)], check=True, capture_output=True, text=True)
            return subprocess.run([str(target)], capture_output=True, text=True)

        baseline = compile_and_run('production', production)
        if baseline.returncode:
            raise RuntimeError(baseline.stderr or baseline.stdout)
        checks['wireless'] = json.loads(baseline.stdout)
        disabled = compile_and_run('disabled', production, wireless=False)
        if disabled.returncode:
            raise RuntimeError(disabled.stderr or disabled.stdout)
        checks['disabled_stub'] = json.loads(disabled.stdout)
        source = production.read_text(encoding='utf-8')
        mutations = {
            'remove_inflight_guard': ('bool available = s_running && !s_inflight;', 'bool available = s_running;'),
            'remove_queue_capacity_guard': ('s_count == RX_CAPACITY)', 'false)'),
            'skip_wifi_deinit': ('err = esp_wifi_deinit();', 'err = ESP_OK;'),
        }
        checks['mutations'] = {}
        for name, (before, after) in mutations.items():
            if source.count(before) != 1:
                raise AssertionError('Mutation anchor changed: ' + name)
            variant = work / (name + '.c')
            variant.write_text(source.replace(before, after, 1), encoding='utf-8')
            failed = compile_and_run(name, variant)
            if failed.returncode == 0:
                raise AssertionError('Fixture missed mutation: ' + name)
            checks['mutations'][name] = {'caught': True, 'exit_code': failed.returncode, 'diagnostic': failed.stderr.strip()}
    bound = [production, api, Path(__file__), fixture, mock]
    report = {
        'schema': 'soundlab-radio-host-fault-validation-v1',
        'validated_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'passed': True,
        'scope': 'Unchanged production C compiled against fake SDK. No real radio, hardware timing or heap acceptance.',
        'real_sdk_build': False,
        'real_device_test': False,
        'two_device_test': False,
        'compiler': str(args.compiler.resolve()),
        'compiler_sha256': digest(args.compiler),
        'sources': {str(p.relative_to(ROOT)).replace('\\', '/'): digest(p) for p in bound},
        'checks': checks,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
