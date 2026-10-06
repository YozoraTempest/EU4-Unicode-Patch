"""Compare the runtime decoder with the audited offline migration tool."""
import importlib.util
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('migration', root / 'tools/migrate-localisation.py')
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)


def cp1252(byte):
    try:
        return bytes([byte]).decode('cp1252')
    except UnicodeDecodeError:
        return chr(byte)


def compare(executable, cases):
    lines = []
    expected = []
    for mode, data, wrapped in cases:
        lines.append(f'{mode} {data.hex()}\n')
        try:
            decoded, counts = migration.decode_legacy(wrapped)
            expected.append(f'OK {counts["sequences"]} {counts["relocated"]} {decoded.encode().hex()}')
        except (ValueError, UnicodeError):
            expected.append(None)
    result = subprocess.run([str(executable)], input=''.join(lines), text=True,
                            encoding='ascii', capture_output=True, check=True)
    actual = result.stdout.splitlines()
    if len(actual) != len(expected):
        raise AssertionError('Decoder returned an incorrect number of results')
    for index, (value, reference) in enumerate(zip(actual, expected)):
        if (reference is None and not value.startswith('ERR ')) or (reference is not None and value != reference):
            raise AssertionError(f'Legacy decoder differs from migration at case {index}: {value} / {reference}')
    return len(actual)


def main():
    cases = []
    # Every payload byte, four offsets, CP1252 undefined bytes, PUA remapping,
    # UTF-16 boundaries and invalid units are compared independently.
    units = set(range(0, 0x10000, 17)) | set(range(0x100)) | {
        0x100, 0x101, 0xd7ff, 0xd800, 0xdbff, 0xdc00, 0xdfff,
        0xe100, 0xe101, 0xe9ff, 0xea00, 0xfffe, 0xffff}
    for marker in range(0x10, 0x14):
        for payload in sorted(units):
            low, high = payload & 0xff, payload >> 8
            wrapped = chr(marker) + cp1252(low) + cp1252(high)
            cases.append(('u', wrapped.encode(), wrapped))
            cases.append(('r', bytes([marker, low, high]), wrapped))
    for text in ['', '\ufeff中文𠀀 é العربية हिन्दी \ue101',
                 '§Y\x10-N\x11\x0eN\x12ùR\x13it§! £adm£ @FRA $COUNTRY$ \\n',
                 '\x10=Ø\x10\x00Þ', '\x10', '\x11a', '\x10中N', '\x10=Ø']:
        cases.append(('u', text.encode(), text))
    count = compare(Path(sys.argv[1]), cases)
    subprocess.run([sys.executable, str(root / 'tests/migration_tests.py')], check=True)
    print(f'PASS: {count} runtime results match the offline legacy migration protocol.')


if __name__ == '__main__':
    main()
