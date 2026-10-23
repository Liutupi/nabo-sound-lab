"""Verify a downloaded v0.1.2 release with Python's standard library only."""
import argparse
import hashlib
import json
import struct
import zipfile
from pathlib import Path, PurePosixPath

def digest(data):
    return hashlib.sha256(data).hexdigest()

def verify(folder, expected_commit=None):
    sums = {}
    for line in (folder / 'SHA256SUMS.txt').read_text(encoding='ascii').splitlines():
        value, name = line.split(None, 1)
        name = name.strip()
        if len(value) != 64 or name != Path(name).name or name in sums:
            raise ValueError('Invalid checksum entry')
        sums[name] = value
        if digest((folder / name).read_bytes()) != value:
            raise ValueError('Checksum mismatch: ' + name)
    manifest_bytes = (folder / 'manifest.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    if manifest['version'] != '0.1.2' or manifest['chip'] != 'esp32c3' or manifest['flash_bytes'] != 8388608:
        raise ValueError('Unexpected version/hardware')
    if expected_commit and manifest['source_commit'] != expected_commit:
        raise ValueError('Wrong source commit')
    if len(manifest['source_commit']) != 40:
        raise ValueError('Missing source commit')
    expected_assets = set(manifest['files']) | {'manifest.json', manifest['archive_name']}
    if set(sums) != expected_assets:
        raise ValueError('Unexpected public asset inventory')
    for name, entry in manifest['files'].items():
        data = (folder / name).read_bytes()
        if len(data) != entry['bytes'] or digest(data) != entry['sha256']:
            raise ValueError('Manifest mismatch: ' + name)
    app = (folder / 'nabo-soundlab-passport.bin').read_bytes()
    if app[0] != 0xe9 or struct.unpack_from('<H', app, 12)[0] != 5:
        raise ValueError('Wrong chip image')
    if struct.unpack_from('<I', app, 0x20)[0] != 0xabcd5432 or app[0x30:0x50].split(b'\0')[0] != b'0.1.2':
        raise ValueError('Wrong app descriptor')
    merged = (folder / 'nabo-soundlab-0.1.2-merged.bin').read_bytes()
    for name, offset in [('bootloader.bin',0),('partition-table.bin',0x8000),
                         ('nabo-soundlab-passport.bin',0x10000),('soundlab_assets.bin',0x200000)]:
        data = (folder / name).read_bytes()
        if merged[offset:offset+len(data)] != data:
            raise ValueError('Merged component mismatch: ' + name)
    if merged[0x9000:0x10000] != b'\xff' * 0x7000:
        raise ValueError('Merged image contains nonblank NVS/PHY')
    with zipfile.ZipFile(folder / manifest['archive_name']) as archive:
        members = archive.namelist()
        if len(members) != len(set(members)) or set(members) != set(manifest['archive_members']):
            raise ValueError('Unexpected archive member inventory')
        for name in members:
            p = PurePosixPath(name)
            if p.is_absolute() or '..' in p.parts or '\\' in name:
                raise ValueError('Unsafe archive path')
        if archive.read('manifest.json') != manifest_bytes:
            raise ValueError('Archive manifest mismatch')
        for name, entry in manifest['files'].items():
            if digest(archive.read(name)) != entry['sha256']:
                raise ValueError('Archive binary mismatch: ' + name)
        for name, value in manifest['archive_document_sha256'].items():
            if digest(archive.read(name)) != value:
                raise ValueError('Archive document mismatch: ' + name)
        bad = archive.testzip()
        if bad:
            raise ValueError('Archive CRC mismatch: ' + bad)
    return {'version':manifest['version'],'source_commit':manifest['source_commit'],
            'verified_release_assets':len(sums)+1,'verified_binary_files':len(manifest['files']),
            'verified_archive_members':len(members),'NVS_PHY_blank':True}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--source-commit')
    args = parser.parse_args()
    print(json.dumps(verify(args.directory, args.source_commit), ensure_ascii=False, indent=2))
