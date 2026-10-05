"""Require every fixed game address and patch write to have a preflight guard."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / 'src/eu4_1375_profile.cpp').read_text(encoding='utf-8')
sites = [(int(rva, 16), bytes.fromhex(value), name) for rva, value, name in
         re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)","([^"]+)"', source)]
ranges_source = source.rsplit('\n        {\n', 1)[1]
ranges = [(int(rva, 16), int(size, 0)) for rva, size in
          re.findall(r'\{(0x[0-9a-f]+),(0x[0-9a-f]+|[0-9]+),ImageAccess::', ranges_source)]
if not sites:
    raise ValueError('Empty executable profile')
guarded = [(rva, len(value)) for rva, value, _ in sites] + ranges

def covered(rva, size=1):
    return any(begin <= rva and rva + size <= begin + length for begin, length in guarded)

for first, value, name in sites:
    for second, other, other_name in sites:
        begin, end = max(first, second), min(first + len(value), second + len(other))
        if begin < end and value[begin-first:end-first] != other[begin-second:end-second]:
            raise ValueError(f'Conflicting guards: {name} / {other_name} at {begin:#x}')

references = 0
for path in (root / 'src').glob('*.cpp'):
    if path.name in ('eu4_1375_profile.cpp', 'executable_compatibility.cpp'):
        continue
    text = path.read_text(encoding='utf-8')
    pattern = r'(?:image|game_image)\s*\+\s*(0x[0-9a-f]+)|address\((0x[0-9a-f]+)\)'
    if path.name == 'native_editor_text.cpp':
        pattern += r'|base\s*\+\s*(0x[0-9a-f]+)'
    for values in re.findall(pattern, text):
        rva = int(next(value for value in values if value), 16)
        if rva < 0x10000:  # Editor object fields are not executable RVAs.
            continue
        references += 1
        if not covered(rva):
            raise ValueError(f'Unguarded game address {rva:#x} in {path.name}')

plugin = (root / 'src/plugin.cpp').read_text(encoding='utf-8')
for rva, before in re.findall(r'\{(0x[0-9a-f]+),bytes\("([0-9a-f]+)"\)', plugin):
    rva, expected = int(rva, 16), bytes.fromhex(before)
    if not any(begin <= rva and rva + len(expected) <= begin + len(value) and
               value[rva-begin:rva-begin+len(expected)] == expected for begin, value, _ in sites):
        raise ValueError(f'Patch write at {rva:#x} does not have a matching byte guard')
print(f'Executable profile: {len(sites)} byte guards; {references} fixed game references and all patch writes covered.')
