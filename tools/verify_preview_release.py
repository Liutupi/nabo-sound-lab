"""Verify an extracted/downloaded v0.2.0-preview.1 asset set (standard library).

Usage: python tools/verify_preview_release.py DIRECTORY [--source-commit SHA]
Checks are explicit and remain enabled under python -O. Never extracts a ZIP,
accesses a device, rebuilds a firmware image, or modifies the supplied directory.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import sys
import zipfile

VERSION = '0.2.0-preview.1'
TAG = 'v' + VERSION
FIRMWARE = '0.2.0-dev'
REVISION = 'fragmented-sprite-cache-v3'
FLASH_BYTES = 8 * 1024 * 1024
# Frozen accepted payload-v3 identities; renaming does not change these bytes.
ANCHORED = {
    'bootloader.bin': (21024, '1a5213932fcd0fe40abe98756afe7edc75080cde37804ee2a5ade7188a94a7ea'),
    'partition-table.bin': (3072, '43311cbc95711332aa26b5e5002a58a9099b1a3be0b394408bc7b2b5dd986d86'),
    'nabo-soundlab-passport.bin': (832512, '5d8440c6494d2464b379c49f0af31e12e658e4e2c80ace6e058325c1308c4d24'),
    'soundlab_assets.bin': (1897404, '5fc895c4ef5ef14a0aa90f40ca027d75806c40f4e7a44860f8538d1d5fa61edb'),
    'nabo-soundlab-0.2.0-preview.1-merged.bin': (3994556, '6085b81101e8d4d5bae8271e538e1ec0fce09e9708051a55e702692141b89243'),
}
OFFSETS = {'bootloader.bin': 0, 'partition-table.bin': 0x8000,
           'nabo-soundlab-passport.bin': 0x10000, 'soundlab_assets.bin': 0x200000}
PNGS = {'duel-device-A-v3-overview.png', 'duel-device-real-rf-overview.png'}
FORBIDDEN = re.compile(r'nvs|backups?|raw[-_]?logs?|private|credentials?|secrets?|\.env|readback|flash[-_]?dump', re.I)


class VerificationError(ValueError):
    pass


def need(ok, message):
    if not ok:
        raise VerificationError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sha_value(value):
    need(isinstance(value, str) and re.fullmatch(r'[0-9a-fA-F]{64}', value) is not None,
         'Invalid SHA256 value')
    return value.lower()


def safe_name(name, basename=False):
    need(isinstance(name, str) and name and '\\' not in name and ':' not in name
         and not any(ord(c) < 32 or ord(c) == 127 for c in name), 'Unsafe path: ' + repr(name))
    path = PurePosixPath(name)
    need(bool(path.parts) and not path.is_absolute() and path.as_posix() == name and all(
         p not in ('.', '..') and p == p.strip() and p == p.rstrip('. ')
         and re.fullmatch(r'(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])', p.split('.')[0]) is None
         for p in path.parts), 'Unsafe path: ' + name)
    need(not basename or len(path.parts) == 1, 'Expected a plain filename: ' + name)
    need(FORBIDDEN.search(name) is None, 'Forbidden private/device-data filename: ' + name)
    return name


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        need(key not in result, 'Duplicate JSON key: ' + key)
        result[key] = value
    return result


def offset_value(value):
    if isinstance(value, str) and re.fullmatch(r'0[xX][0-9a-fA-F]+', value):
        return int(value, 16)
    need(type(value) is int and value >= 0, 'Invalid component offset')
    return value


def parse_sums(data):
    result = {}
    for line in data.decode('ascii').splitlines():
        found = re.fullmatch(r'([0-9a-fA-F]{64}) [ *]([A-Za-z0-9][A-Za-z0-9._-]*)', line)
        need(found is not None, 'Malformed SHA256SUMS.txt line: ' + repr(line))
        name = safe_name(found[2], basename=True)
        need(name not in result, 'Duplicate checksum entry: ' + name)
        result[name] = found[1].lower()
    need(bool(result), 'Empty checksum inventory')
    return result


def check_image(data, name, descriptor=False):
    need(len(data) >= 0x120 and data[0] == 0xe9, name + ': invalid ESP image header')
    need(struct.unpack_from('<H', data, 12)[0] == 5, name + ': image is not ESP32-C3 (chip 5)')
    need(data[3] & 0xf0 == 0x30, name + ': image header does not specify 8MB flash')
    if descriptor:
        need(struct.unpack_from('<I', data, 0x20)[0] == 0xabcd5432, 'Invalid ESP app descriptor magic')
        version = data[0x30:0x50]
        need(version == FIRMWARE.encode('ascii').ljust(32, b'\0'), 'Wrong internal app descriptor version')


def check_merged(data, components):
    need(len(data) <= FLASH_BYTES, 'Merged image exceeds 8MB flash')
    cursor, gaps = 0, []
    for name, offset in sorted(OFFSETS.items(), key=lambda pair: pair[1]):
        component = components[name]
        need(offset >= cursor and offset + len(component) <= len(data), 'Merged overlap/out-of-range: ' + name)
        need(data[cursor:offset] == b'\xff' * (offset - cursor),
             f'Non-FF merged gap at 0x{cursor:x}..0x{offset:x}')
        gaps.append([cursor, offset])
        need(data[offset:offset + len(component)] == component, 'Merged component mismatch: ' + name)
        cursor = offset + len(component)
    need(data[cursor:] == b'\xff' * (len(data) - cursor), 'Non-FF trailing merged gap')
    need(data[0x9000:0x10000] == b'\xff' * 0x7000, 'Merged image contains nonblank NVS/PHY')
    return gaps + [[cursor, len(data)]]


def check_archive(path, manifest_bytes, files, documents, expected_members):
    sizes = {}
    with zipfile.ZipFile(path) as archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        need(len(names) == len(set(names)) == len({n.casefold() for n in names}),
             'Duplicate or case-colliding ZIP members')
        need(set(names) == expected_members, 'ZIP member inventory differs from manifest/files/documents')
        for info in infos:
            name = safe_name(info.filename)
            need(info.orig_filename == name and not info.is_dir(), 'Invalid/directory ZIP member: ' + name)
            mode = (info.external_attr >> 16) & 0o170000
            need(mode in (0, stat.S_IFREG), 'Non-regular ZIP member: ' + name)
            need(not info.flag_bits & 1, 'Encrypted ZIP member: ' + name)
            data = archive.read(info)  # zipfile validates each member's CRC while reading.
            need(len(data) == info.file_size, 'ZIP member size mismatch: ' + name)
            if name == 'manifest.json':
                need(data == manifest_bytes, 'ZIP manifest bytes differ from external manifest')
            elif name in files:
                need(data == files[name], 'ZIP attachment bytes differ: ' + name)
            else:
                need(digest(data) == documents[name], 'ZIP document digest mismatch: ' + name)
            sizes[name] = len(data)
        bad = archive.testzip()
        need(bad is None, 'ZIP CRC failure: ' + str(bad))
    return sizes


def verify(directory, expected_commit=None):
    directory = Path(directory)
    need(directory.is_dir(), 'Release directory does not exist')
    if expected_commit is not None:
        need(re.fullmatch(r'[0-9a-fA-F]{40}', expected_commit) is not None, 'Expected commit must be 40 hex digits')
        expected_commit = expected_commit.lower()
    manifest_path = directory / 'manifest.json'
    need(manifest_path.is_file() and not manifest_path.is_symlink(), 'Missing/linked manifest.json')
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes, object_pairs_hook=unique_object)
    need(isinstance(manifest, dict), 'Manifest must be a JSON object')
    required = {'schema': 'nabo-sound-lab-release-v2', 'release_tag': TAG, 'version': VERSION,
                'firmware_version': FIRMWARE, 'channel': 'prerelease', 'chip': 'esp32c3',
                'flash_bytes': FLASH_BYTES, 'revision': REVISION}
    for key, value in required.items():
        need(manifest.get(key) == value, 'Unexpected/missing manifest ' + key)
    commit = manifest.get('source_commit')
    need(isinstance(commit, str) and re.fullmatch(r'[0-9a-fA-F]{40}', commit) is not None,
         'Manifest source_commit must be 40 hex digits')
    commit = commit.lower()
    need(expected_commit is None or commit == expected_commit, 'Wrong source commit')
    entries = manifest.get('files')
    need(isinstance(entries, dict) and set(entries) == set(ANCHORED) | PNGS, 'Unexpected manifest file inventory')
    archive_name = safe_name(manifest.get('archive_name'), basename=True)
    need(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*\.zip', archive_name) is not None, 'Invalid archive_name')
    documents = manifest.get('archive_document_sha256')
    need(isinstance(documents, dict) and bool(documents), 'Missing archive documents')
    documents = {safe_name(n): sha_value(s) for n, s in documents.items()}
    reserved = set(entries) | {'manifest.json', 'SHA256SUMS.txt', archive_name}
    need(not reserved.intersection(documents), 'Archive document overlaps reserved member')
    for name in documents:
        need(PurePosixPath(name).suffix.lower() in ('.md', '.txt', '.json', '.py')
             or PurePosixPath(name).name in ('LICENSE', 'NOTICE', 'COPYING')
             or name == 'firmware/LICENSE.FoloToy', 'Non-document ZIP member: ' + name)
    expected_members = set(entries) | {'manifest.json'} | set(documents)
    need(len(expected_members) == len({n.casefold() for n in expected_members}), 'Case-colliding archive inventory')
    members = manifest.get('archive_members')
    need(isinstance(members, list) and all(isinstance(n, str) for n in members), 'Invalid archive_members')
    need(len(members) == len(set(members)) and set(members) == expected_members, 'Inexact archive_members inventory')
    for name in members:
        safe_name(name)
    expected_sums = set(entries) | {'manifest.json', archive_name}
    expected_attachments = expected_sums | {'SHA256SUMS.txt'}
    children = list(directory.iterdir())
    need(all(p.is_file() and not p.is_symlink() for p in children), 'Release directory contains a link/directory')
    need({p.name for p in children} == expected_attachments, 'Unexpected/missing local release attachment inventory')
    sums = parse_sums((directory / 'SHA256SUMS.txt').read_bytes())
    need(set(sums) == expected_sums, 'Checksum inventory must enumerate every attachment except itself')
    attachments = {name: (directory / name).read_bytes() for name in expected_sums}
    for name, data in attachments.items():
        need(digest(data) == sums[name], 'Attachment checksum mismatch: ' + name)
    files = {}
    for name, entry in entries.items():
        need(isinstance(entry, dict) and type(entry.get('bytes')) is int and entry['bytes'] > 0,
             'Invalid manifest size: ' + name)
        data = attachments[name]
        need(len(data) == entry['bytes'] and digest(data) == sha_value(entry.get('sha256')),
             'Manifest attachment size/digest mismatch: ' + name)
        if name in OFFSETS:
            need(offset_value(entry.get('offset')) == OFFSETS[name], 'Wrong component offset: ' + name)
        else:
            need('offset' not in entry, 'Unexpected component offset: ' + name)
        if name in ANCHORED:
            need((len(data), digest(data)) == ANCHORED[name], 'Accepted V3 binary identity mismatch: ' + name)
        else:
            need(data.startswith(b'\x89PNG\r\n\x1a\n'), 'Invalid PNG attachment: ' + name)
        files[name] = data
    check_image(files['bootloader.bin'], 'bootloader.bin')
    check_image(files['nabo-soundlab-passport.bin'], 'nabo-soundlab-passport.bin', descriptor=True)
    need(files['soundlab_assets.bin'].startswith(b'FNAS'), 'Invalid FNAS asset magic')
    gaps = check_merged(files['nabo-soundlab-0.2.0-preview.1-merged.bin'], files)
    sizes = check_archive(directory / archive_name, manifest_bytes, files, documents, expected_members)
    return {'status': 'passed', 'version': VERSION, 'release_tag': TAG, 'firmware_version': FIRMWARE,
            'revision': REVISION, 'channel': 'prerelease', 'chip': 'esp32c3', 'flash_bytes': FLASH_BYTES,
            'source_commit': commit, 'verified_release_assets': len(expected_attachments),
            'verified_binary_files': len(ANCHORED), 'verified_png_files': len(PNGS),
            'verified_archive_members': len(sizes), 'archive_member_bytes': sizes,
            'accepted_v3_binary_sha256': {n: value[1] for n, value in ANCHORED.items()},
            'NVS_PHY_blank': True, 'all_noncomponent_gaps_ff': True, 'merged_gap_ranges': gaps}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--source-commit')
    args = parser.parse_args()
    try:
        result = verify(args.directory, args.source_commit)
    except (VerificationError, OSError, ValueError, TypeError, KeyError, zipfile.BadZipFile,
            NotImplementedError, RuntimeError) as exc:
        print(json.dumps({'status': 'failed', 'error': str(exc)}, ensure_ascii=False, indent=2))
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
