"""Exercise EU4's real registry and mounts in a newly owned Windows runtime.

Build the DLLs in CI first. This script never edits the source installation,
Workshop content, live descriptors, or live userdir. Process inspection uses
OpenProcess/ReadProcessMemory only; no injection or remote calls are made.
"""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
import zipfile

EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'
MAIN = 'Chinese Language Mod for 1.37'
SUPPLEMENT = 'Chinese Language Supplementary Mod for 1.37'
PROBE = 'ZZZ Registry Dependency Probe'
PROBE_RIVAL = '宿敌（依赖覆盖验证）'
IDS = ['3813976975', '3813977176', '2976470733', '3290499798', '3301109455', '3467086805']


def sha(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()


SCRIPT_SHA256 = sha(Path(__file__))


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


class Reader:
    """Verified 1.37.5 ABI, scoped to the subprocess handle owned by this script."""
    def __init__(self, pid):
        self.kernel = C.WinDLL('kernel32', use_last_error=True)
        self.psapi = C.WinDLL('psapi', use_last_error=True)
        self.kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        self.kernel.OpenProcess.restype = W.HANDLE
        self.kernel.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
        self.kernel.CloseHandle.argtypes = [W.HANDLE]
        self.psapi.EnumProcessModules.argtypes = [W.HANDLE, C.POINTER(W.HMODULE), W.DWORD, C.POINTER(W.DWORD)]
        self.psapi.GetModuleFileNameExW.argtypes = [W.HANDLE, W.HMODULE, W.LPWSTR, W.DWORD]
        self.handle = self.kernel.OpenProcess(0x1010, False, pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())

    def close(self):
        self.kernel.CloseHandle(self.handle)

    def read(self, address, size):
        data = C.create_string_buffer(size)
        count = C.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, data, size, C.byref(count)) or count.value != size:
            raise C.WinError(C.get_last_error())
        return data.raw

    def pointer(self, address):
        return struct.unpack('<Q', self.read(address, 8))[0]

    def string(self, address):
        raw = self.read(address, 32)
        size, capacity = struct.unpack_from('<QQ', raw, 16)
        if size > 32768:
            raise ValueError('Invalid engine string size')
        return (raw[:size] if capacity < 16 else self.read(struct.unpack_from('<Q', raw)[0], size)).decode('utf-8')

    def strings(self, address):
        pointer = self.pointer(address + 8)
        count = struct.unpack('<i', self.read(address + 20, 4))[0]
        if not 0 <= count <= 1000:
            raise ValueError('Invalid native string list size')
        return [self.string(pointer + i * 32) for i in range(count)]

    def vector_strings(self, address):
        start, end = struct.unpack('<QQ', self.read(address, 16))
        if end < start or (end - start) % 32 or end - start > 32000:
            raise ValueError('Invalid native string vector size')
        return [self.string(item) for item in range(start, end, 32)]

    def state(self):
        modules = (W.HMODULE * 1024)()
        needed = W.DWORD()
        if not self.psapi.EnumProcessModules(self.handle, modules, C.sizeof(modules), C.byref(needed)):
            raise C.WinError(C.get_last_error())
        if not needed.value or needed.value > C.sizeof(modules) or not modules[0]:
            raise OSError('Process module list is not available yet')
        paths = []
        for module in modules[:needed.value // C.sizeof(W.HMODULE)]:
            name = C.create_unicode_buffer(32768)
            self.psapi.GetModuleFileNameExW(self.handle, module, name, len(name))
            paths.append(name.value)
        base = modules[0]
        app = self.pointer(base + 0x242bf50)
        state = {'schema_version': 2, 'reader_sha256': SCRIPT_SHA256,
                 'modules': paths, 'frida_loaded': any('frida' in path.lower() for path in paths),
                 'checksum_ready': bool(app and self.read(app + 0x328, 1)[0])}
        if app:
            idler = self.pointer(app + 0x40)
            state['idler_vtable_rva'] = hex(self.pointer(idler) - base) if idler else None
        manager = self.pointer(base + 0x2349520)
        if manager and state['checksum_ready']:
            state['enabled_mods'] = self.strings(manager + 0x88)
            head = self.pointer(manager + 0xd0)
            pending, seen, entries = [self.pointer(head + 8)], set(), []
            while pending:
                node = pending.pop()
                if node == head or node in seen:
                    continue
                seen.add(node)
                if len(seen) > 1000:
                    raise ValueError('Invalid native mod tree size')
                pending.extend([self.pointer(node), self.pointer(node + 16)])
                mod = node + 0x40
                entries.append({'key': self.string(node + 0x20), 'name': self.string(mod + 0x10),
                                'descriptor': self.string(mod + 0x90), 'directory': self.string(mod + 0xb0),
                                'archive': self.string(mod + 0xd0),
                                'dependencies': self.vector_strings(mod + 0xf0)})
            state['registry'] = sorted(entries, key=lambda value: value['key'])
        return state


def prepare(args):
    root, game = args.root.resolve(), args.game.resolve()
    if root.exists() and any(root.iterdir()):
        raise ValueError('Preparation needs an empty new evidence directory')
    assert sha(game / 'eu4.exe') == EXE_HASH
    root.mkdir(parents=True, exist_ok=True)
    runtime = root / 'runtime'
    runtime.mkdir()
    (root / 'owned-runtime.json').write_text(json.dumps({'runtime': str(runtime), 'exe_sha256': EXE_HASH}), encoding='utf-8')
    for source in game.iterdir():
        if source.is_file() and source.suffix.lower() in {'.dll', '.exe', '.txt', '.json', '.manifest', '.dds'}:
            if source.name not in {'userdir.txt', 'console_history.txt', 'trigger_profile.txt'}:
                shutil.copy2(source, runtime / source.name)
        elif source.is_dir() and source.name not in {'plugins', 'gfx', 'tools', 'tests'}:
            subprocess.run(['cmd', '/c', 'mklink', '/J', str(runtime / source.name), str(source)], check=True, capture_output=True)
    # The patch writes font caches here. Keep fonts private, and share other
    # asset directories read-only through junctions, as in the player workflow.
    (runtime / 'gfx').mkdir()
    for source in (game / 'gfx').iterdir():
        target = runtime / 'gfx' / source.name
        if source.name == 'fonts':
            shutil.copytree(source, target, ignore=shutil.ignore_patterns('cache'))
        elif source.is_dir():
            subprocess.run(['cmd', '/c', 'mklink', '/J', str(target), str(source)], check=True, capture_output=True)
        else:
            shutil.copy2(source, target)
    (runtime / 'plugins').mkdir()
    snapshot = {'game': str(game), 'exe_sha256': EXE_HASH,
                'branch': (game / 'eu4_branch.txt').read_text().strip(),
                'revision': (game / 'eu4_rev.txt').read_text().strip(), 'sources': {}}
    sources = {value: args.workshop / value for value in IDS}
    sources['local'] = args.upload
    for key, source in sources.items():
        descriptor = source / 'descriptor.mod'
        if not descriptor.is_file():
            raise ValueError(f'Missing current mod source: {descriptor}')
        snapshot['sources'][key] = {'path': str(source), 'descriptor': descriptor.read_text(encoding='utf-8-sig'),
                                    'descriptor_sha256': sha(descriptor)}
    snapshot['font_files'] = {name: sha(sources['3813976975'] / name)
                              for name in ['gfx/fonts/zh-hans-map.fnt', 'gfx/fonts/zh-hans-map.dds']}
    # All descriptor edits below belong to these new fixtures. Source mods
    # remain read-only, including their original names and dependencies.
    assets = root / 'fixture-assets'
    assets.mkdir()
    archive = assets / 'main.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=1) as package:
        for file in sorted(sources['3813976975'].rglob('*')):
            if file.is_file():
                package.write(file, file.relative_to(sources['3813976975']).as_posix())
    probe = assets / 'dependency-probe'
    localisation = Path('localisation/diplomacy_l_english.yml')
    source_text = (sources['3813976975'] / localisation).read_text(encoding='utf-8-sig')
    probe_text, replacements = re.subn(r'(?m)^(\s*rival:\d*\s*)"[^"]*"',
                                      lambda match: match[1] + '"' + PROBE_RIVAL + '"', source_text)
    assert replacements == 1, 'The dependency probe needs exactly one rival translation'
    (probe / localisation).parent.mkdir(parents=True)
    (probe / localisation).write_text(probe_text, encoding='utf-8-sig')
    snapshot['fixture_assets'] = {'archive_sha256': sha(archive),
                                  'probe_sha256': sha(probe / localisation),
                                  'probe_source_sha256': sha(sources['3813976975'] / localisation)}
    snapshot['preparer_sha256'] = SCRIPT_SHA256
    save(root / 'sources-before.json', snapshot)
    common = [('supplement.mod', '3813977176', SUPPLEMENT),
              ('proper2k.mod', '3290499798', 'Proper 2K UI Enhanced'),
              ('compat.mod', '3301109455', 'P2KE Patch - Chinese Language Mod'),
              ('font.mod', '3467086805', 'P2KE-FangZhengCuYuan')]
    cases = {
        'unique': [('main.mod', '3813976975', True)],
        'old-disabled-first': [('a_old.mod', '2976470733', False), ('z_utf8.mod', '3813976975', True)],
        'old-enabled-first': [('a_utf8.mod', '3813976975', True), ('z_old.mod', '2976470733', False)],
        'local-disabled-first': [('a_local.mod', 'local', False), ('z_utf8.mod', '3813976975', True)],
        'local-enabled-first': [('a_utf8.mod', '3813976975', True), ('z_local.mod', 'local', False)],
        'three-candidates': [('a_local.mod', 'local', False), ('m_old.mod', '2976470733', False), ('z_utf8.mod', '3813976975', True)],
        'none-enabled': [('a_old.mod', '2976470733', False), ('z_utf8.mod', '3813976975', False)],
        'multiple-enabled': [('a_old.mod', '2976470733', True), ('z_utf8.mod', '3813976975', True)],
        'multiple-enabled-inactive-first': [('a_local.mod', 'local', False), ('m_old.mod', '2976470733', True), ('z_utf8.mod', '3813976975', True)],
        'archive-disabled-first': [('a_old.mod', '2976470733', False), ('z_archive.mod', 'archive', True)],
        'archive-enabled-first': [('a_archive.mod', 'archive', True), ('z_old.mod', '2976470733', False)],
        'dependency-forward': [('a_old.mod', '2976470733', False), ('z_utf8.mod', '3813976975', True)],
        'dependency-reversed': [('a_old.mod', '2976470733', False), ('z_utf8.mod', '3813976975', True)],
        'dependency-control': [('main.mod', '3813976975', True)],
    }
    settings = args.settings.read_text(encoding='utf-8')
    settings = settings.replace('x=2560', 'x=1280').replace('y=1440', 'y=720')
    settings = settings.replace('borderless=yes', 'borderless=no').replace('multi_sampling=8', 'multi_sampling=0')
    for name, candidates in cases.items():
        userdir = root / 'fixtures' / name
        (userdir / 'mod').mkdir(parents=True)
        # Keep the order probe independent of UI/font mods that themselves
        # depend on MAIN and would raise its rank even in the no-dependency control.
        case_common = [] if name.startswith('dependency-') else common
        enabled = [f'mod/{file}' for file, _, _ in case_common]
        for file, key, label in case_common + [(file, key, MAIN) for file, key, _ in candidates]:
            source = sources['3813976975' if key == 'archive' else key]
            text = (source / 'descriptor.mod').read_text(encoding='utf-8-sig')
            text = re.sub(r'(?m)^name\s*=.*$', f'name="{label}"', text)
            text = re.sub(r'(?m)^(path|archive)\s*=.*\n?', '', text)
            for original, target in [(f'[UTF8] {MAIN}', MAIN), (f'[UTF8] {SUPPLEMENT}', SUPPLEMENT),
                                     ('Chinese Mod - 2K UI Project Enhanced', 'P2KE Patch - Chinese Language Mod')]:
                text = text.replace(f'"{original}"', f'"{target}"')
            text += f'\narchive="{archive.as_posix()}"\n' if key == 'archive' else f'\npath="{source.as_posix()}"\n'
            (userdir / 'mod' / file).write_text(text, encoding='utf-8')
        enabled += [f'mod/{file}' for file, _, active in candidates if active]
        expected_rival = None
        if name.startswith('dependency-'):
            dependency = f'dependencies={{"{MAIN}"}}\n' if name != 'dependency-control' else ''
            (userdir / 'mod/probe.mod').write_text(
                f'name="{PROBE}"\npath="{probe.as_posix()}"\n{dependency}', encoding='utf-8')
            enabled.insert(0, 'mod/probe.mod')
            if name == 'dependency-reversed':
                enabled.reverse()
            expected_rival = PROBE_RIVAL if dependency else '宿敌'
        save(userdir / 'dlc_load.json', {'enabled_mods': enabled, 'disabled_dlcs': []})
        (userdir / 'settings.txt').write_text(settings, encoding='utf-8')
        save(userdir / 'test.json', {'name': name, 'candidates': candidates, 'enabled_mods': enabled,
                                   'expected_rival': expected_rival})
    print(f'Prepared {len(cases)} fixtures in {root}', flush=True)


def run(args):
    root = args.root.resolve()
    runtime = root / 'runtime'
    owner = json.loads((root / 'owned-runtime.json').read_text())
    assert Path(owner['runtime']).resolve() == runtime and sha(runtime / 'eu4.exe') == EXE_HASH
    result_dir = root / args.label / args.case
    if result_dir.exists():
        raise ValueError('Preserve evidence: choose a new result label')
    userdir = result_dir / 'userdir'
    shutil.copytree(root / 'fixtures' / args.case, userdir)
    with zipfile.ZipFile(args.package) as package:
        for name in ['VERSION.dll', 'plugins/eu4_unicode_patch.dll']:
            (runtime / name).write_bytes(package.read(name))
    for path in (runtime / 'plugins').glob('*.log'):
        path.unlink()  # This directory belongs only to this new runtime.
    (runtime / 'userdir.txt').write_text(str(userdir), encoding='utf-8')
    # Direct launches need the App ID; automated runs keep native errors in logs.
    (runtime / 'steam_appid.txt').write_text('236850\n', encoding='ascii')
    save(result_dir / 'launch.json', {'exe': str(runtime / 'eu4.exe'), 'exe_sha256': EXE_HASH,
                                    'runner_sha256': SCRIPT_SHA256, 'snapshot_schema_version': 2,
                                    'package': str(args.package), 'package_sha256': sha(args.package),
                                    'dll_sha256': sha(runtime / 'plugins/eu4_unicode_patch.dll'),
                                    'loader_sha256': sha(runtime / 'VERSION.dll'),
                                    'steam_appid_sha256': sha(runtime / 'steam_appid.txt'), 'userdir': str(userdir),
                                    'enabled_mods': json.loads((userdir / 'dlc_load.json').read_text())})
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([str(runtime / 'eu4.exe'), '-debug', '-nointro', '-skiplauncher',
                                '-suppresserrormessages'],
                               cwd=runtime, startupinfo=startup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    reader, state = None, {}
    began = time.monotonic()
    try:
        while time.monotonic() - began < args.timeout:
            if process.poll() is not None:
                raise RuntimeError(f'Owned game exited early: {process.returncode}')
            try:
                if not reader:
                    reader = Reader(process.pid)
                state = reader.state()
                if state.get('checksum_ready'):
                    break
            except OSError:
                pass  # Loading has not established every native pointer yet.
            time.sleep(2)
        else:
            raise TimeoutError('Owned game did not complete startup')
        assert not state['frida_loaded']
        # Wait for the native main menu, so fonts and setup logs are complete.
        deadline = time.monotonic() + 30
        while state.get('idler_vtable_rva') != '0x1d5a8c8' and time.monotonic() < deadline:
            time.sleep(2)
            state = reader.state()
        state['pid'] = process.pid
        state['elapsed_seconds'] = round(time.monotonic() - began, 2)
        save(result_dir / 'native-state.json', state)
    finally:
        if reader:
            reader.close()
        # Never enumerate/terminate other EU4 instances; this handle was created
        # above and refers solely to this script's isolated game subprocess.
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=30)
        for log in (runtime / 'plugins').glob('*.log'):
            shutil.copy2(log, result_dir / log.name)
    setup = (userdir / 'logs/setup.log').read_bytes()
    error = (userdir / 'logs/error.log').read_bytes()
    patch = (result_dir / 'eu4_unicode_patch.log').read_text(encoding='utf-8')
    opinion = [line.decode('utf-8') for line in setup.splitlines() if b'Opinion Modifier #' in line and
               (b'tag = rival ' in line or b'tag = enemy ' in line)]
    main = next(value for value in state['registry'] if value['name'] == MAIN)
    summary = {'case': args.case, 'label': args.label, 'menu_ready': state.get('idler_vtable_rva') == '0x1d5a8c8',
               'selected': main['descriptor'], 'opinion': opinion,
               'chinese': any('name = 宿敌' in line for line in opinion) and any('name = 敌对关系' in line for line in opinion),
               'map_font_missing': b'zh-hans-map.fnt' in error,
               'map_font_retained': 'Mod font retained: gfx/fonts/zh-hans-map' in patch,
               'collisions': [line for line in patch.splitlines() if 'Mod name collision:' in line],
               'repair_errors': [line for line in patch.splitlines() if 'Mod collision' in line and
                                 ('failed' in line or 'rejected' in line)],
               'dll_sha256': sha(runtime / 'plugins/eu4_unicode_patch.dll')}
    save(result_dir / 'result.json', summary)
    print(json.dumps(summary, ensure_ascii=False), flush=True)


def compare(args):
    """Assert behavior from native process snapshots and real resource logs."""
    root = args.root.resolve()
    rows = []
    for fixture in sorted((root / 'fixtures').iterdir()):
        definition = json.loads((fixture / 'test.json').read_text(encoding='utf-8'))
        candidates = definition['candidates']
        enabled = [file for file, _, active in candidates if active]
        first = f'mod/{candidates[0][0]}'
        selected = f'mod/{enabled[0]}' if len(enabled) == 1 else first
        before_dir, after_dir = root / args.before / fixture.name, root / args.after / fixture.name
        before = json.loads((before_dir / 'result.json').read_text(encoding='utf-8'))
        after = json.loads((after_dir / 'result.json').read_text(encoding='utf-8'))
        baseline = json.loads((before_dir / 'native-state.json').read_text(encoding='utf-8'))
        patched = json.loads((after_dir / 'native-state.json').read_text(encoding='utf-8'))
        assert before['menu_ready'] and after['menu_ready'], fixture.name
        assert baseline['schema_version'] == patched['schema_version'] == 2, fixture.name
        assert baseline['enabled_mods'] == patched['enabled_mods'] == definition['enabled_mods'], fixture.name
        assert before['selected'] == (selected if args.before_fixed else first) and after['selected'] == selected, fixture.name
        normal = len(enabled) == 1 or (len(enabled) > 1 and candidates[0][2])
        assert after['chinese'] == normal and after['map_font_missing'] != normal, fixture.name
        assert not after['repair_errors'], fixture.name
        if definition['expected_rival']:
            assert any(line.endswith('name = ' + definition['expected_rival']) for line in after['opinion']), fixture.name
        if len(enabled) != 1:
            assert (before['selected'], before['opinion'], before['map_font_missing']) == (
                after['selected'], after['opinion'], after['map_font_missing']), fixture.name
        assert len(after['collisions']) == (1 if len(candidates) > 1 else 0), fixture.name
        if after['collisions']:
            rule = 'unique-enabled' if len(enabled) == 1 else 'native-none-enabled' if not enabled else 'native-multiple-enabled'
            assert f'rule={rule}' in after['collisions'][0], fixture.name
            assert f'replaced={"yes" if selected != first else "no"}' in after['collisions'][0], fixture.name
            # The log retains actual registration order, independently of the
            # intended filenames or creation order in the test definition.
            positions = [after['collisions'][0].index(f'"mod/{file}"') for file, _, _ in candidates]
            assert positions == sorted(positions), fixture.name
        names = lambda state: {item['name']: (item['key'], item['dependencies']) for item in state['registry']}
        assert names(baseline) == names(patched), fixture.name
        if fixture.name.startswith('dependency-'):
            probe = next(item for item in patched['registry'] if item['name'] == PROBE)
            assert probe['dependencies'] == ([] if fixture.name == 'dependency-control' else [MAIN]), fixture.name
        else:
            adapter = next(item for item in patched['registry'] if item['name'] == 'P2KE Patch - Chinese Language Mod')
            assert MAIN in adapter['dependencies'] and SUPPLEMENT in adapter['dependencies'], fixture.name
        for name in [MAIN] + ([SUPPLEMENT] if not fixture.name.startswith('dependency-') else []):
            assert name in names(patched) and names(patched)[name][0] == name, fixture.name
        assert all(dependency in names(patched) for item in patched['registry']
                   for dependency in item['dependencies']), fixture.name
        main = next(item for item in patched['registry'] if item['name'] == MAIN)
        descriptor = (after_dir / 'userdir' / selected).read_text(encoding='utf-8')
        resource = re.search(r'(?m)^(path|archive)="([^"]+)"', descriptor)
        field = 'directory' if resource[1] == 'path' else 'archive'
        assert Path(main[field]).resolve() == Path(resource[2]).resolve(), fixture.name
        assert not main['archive' if field == 'directory' else 'directory'], fixture.name
        if args.before_fixed:
            assert (before['selected'], before['opinion'], before['map_font_missing']) == (
                after['selected'], after['opinion'], after['map_font_missing']), fixture.name
        assert not baseline['frida_loaded'] and not patched['frida_loaded'], fixture.name
        rows.append({'case': fixture.name, 'before': before, 'after': after, 'passed': True})
    save(root / 'comparison.json', {'passed': True, 'groups': len(rows), 'rows': rows})
    print(f'Passed {len(rows)} native registry/resource comparisons.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prepare_parser = commands.add_parser('prepare')
    prepare_parser.add_argument('--root', type=Path, required=True)
    prepare_parser.add_argument('--game', type=Path, required=True)
    prepare_parser.add_argument('--workshop', type=Path, required=True)
    prepare_parser.add_argument('--upload', type=Path, required=True)
    prepare_parser.add_argument('--settings', type=Path, required=True)
    run_parser = commands.add_parser('run')
    run_parser.add_argument('--root', type=Path, required=True)
    run_parser.add_argument('--case', required=True)
    run_parser.add_argument('--label', required=True)
    run_parser.add_argument('--package', type=Path, required=True)
    run_parser.add_argument('--timeout', type=int, default=240)
    compare_parser = commands.add_parser('compare')
    compare_parser.add_argument('--root', type=Path, required=True)
    compare_parser.add_argument('--before', required=True)
    compare_parser.add_argument('--after', required=True)
    compare_parser.add_argument('--before-fixed', action='store_true',
                                help='Compare against a CI package that already includes the collision fix')
    args = parser.parse_args()
    {'prepare': prepare, 'run': run, 'compare': compare}[args.command](args)


if __name__ == '__main__':
    main()
