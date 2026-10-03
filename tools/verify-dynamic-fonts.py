"""Validate controlled native commits and exact Direct3D font texture pixels."""
import argparse
import hashlib
import json
from pathlib import Path
import re

SAMPLES = ['中华人民共和国', '孔雀翡翠', '𠮷𰀀𲎰']
EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'


def verify(trace_path, cpu_path, *, font='gfx/fonts/zh-hans-16'):
    records = [json.loads(line) for line in trace_path.read_text(encoding='utf-8').splitlines() if line]
    artifacts = [record for record in records if record.get('event') == 'artifact']
    if len(artifacts) != 1 or artifacts[0].get('exe_sha256') != EXE_HASH:
        raise ValueError('Missing or incorrect native artifact fingerprint')
    artifact = artifacts[0]
    if not re.fullmatch('[0-9a-f]{64}', artifact.get('dll_sha256', '')):
        raise ValueError('Missing DLL fingerprint')
    if artifact.get('samples') != SAMPLES or any(record.get('type') == 'error' for record in records):
        raise ValueError('Unexpected sample scope or agent error')
    payloads = [record['payload'] for record in records if record.get('type') == 'send']
    if any(p.get('event') in ('sdl-validation-failed', 'dynamic-font-failed') for p in payloads):
        raise ValueError('Native font probe failed')
    commits = [p for p in payloads if p.get('event') == 'native-sdl-result']
    deliveries = [p for p in payloads if p.get('event') == 'synthetic-sdl-textinput']
    if len(commits) != 3 or len(deliveries) != 3:
        raise ValueError('Missing or duplicate native commits')
    for text, commit, delivery in zip(SAMPLES, commits, deliveries):
        encoded = list(text.encode('utf-8'))
        if commit.get('after') != encoded or commit.get('after_caret') != len(encoded) or commit.get('inserts') != [encoded] or commit.get('notifications') != [encoded]:
            raise ValueError('Native text, caret or insertion differs from the committed UTF-8 sample')
        if delivery.get('payload') != encoded or delivery.get('window_id', 0) <= 0:
            raise ValueError('SDL delivery differs from its sample')
        if commit.get('queue_copies') != [encoded+[0]*(32-len(encoded))]*2:
            raise ValueError('Native SDL queue did not preserve the complete commit')
    complete = [p for p in payloads if p.get('event') == 'sdl-validation-complete']
    if len(complete) != 1 or complete[0].get('cases') != 3:
        raise ValueError('Missing native completion')
    cpu = json.loads(cpu_path.read_text())
    if cpu.get('size') != 16:
        raise ValueError('Unexpected reference font size')
    scalars = sorted(set(map(ord, ''.join(SAMPLES))))
    reference = {glyph['scalar']: glyph for glyph in cpu['glyphs']}
    if sorted(reference) != scalars or len(cpu['glyphs']) != len(scalars):
        raise ValueError('Incomplete or duplicate CPU reference')
    gpu = [p for p in payloads if p.get('event') == 'dynamic-font-gpu']
    if len(gpu) != 3 or sorted(p['text'] for p in gpu) != sorted(SAMPLES):
        raise ValueError('Missing or duplicate native GPU readback')
    actual, glyphs = set(), []
    for sample in gpu:
        if sample.get('font') != font or sample.get('dimensions') != [2048, 4096] or sample.get('format') != 21 or sample.get('pool') != 0:
            raise ValueError('Unexpected native GPU font texture')
        if not re.fullmatch('0x[0-9a-f]+', sample.get('draw_return', '')):
            raise ValueError('Missing observed draw caller')
        target = sorted(set(map(ord, sample['text'])))
        if sorted(g['scalar'] for g in sample['glyphs']) != target:
            raise ValueError('GPU readback does not match its text')
        for glyph in sample['glyphs']:
            scalar = glyph['scalar']
            if scalar in actual:
                raise ValueError('Duplicate GPU scalar')
            actual.add(scalar)
            ref = reference[scalar]
            metrics = glyph.get('metrics', [])
            if len(metrics) != 7 or metrics[2:] != ref['metrics'] or glyph.get('alpha') != ref['alpha'] or not any(glyph['alpha']):
                raise ValueError('Native glyph metrics or actual GPU pixels differ from the raster')
            x, y, width, height, *_ = metrics
            if x < 1 or y < 1 or x+width > 2048 or y+height > 4096:
                raise ValueError('Glyph region exceeds the native atlas')
            operations = glyph.get('operations', {})
            if sorted(operations) != sorted(['create_target', 'stretch', 'create_readback', 'readback', 'lock', 'unlock']) or any(operations.values()):
                raise ValueError('GPU readback operation failed or is missing')
            if glyph.get('ellipsis_same') is not False:
                raise ValueError('Native lookup returned an ellipsis placeholder')
            glyphs.append(dict(scalar=scalar, family=ref['family'], metrics=metrics,
                               alpha_sha256=hashlib.sha256(bytes(glyph['alpha'])).hexdigest()))
    if sorted(actual) != scalars:
        raise ValueError('Missing GPU scalars')
    return dict(exe_sha256=EXE_HASH, dll_sha256=artifact['dll_sha256'], samples=SAMPLES,
                native_commits=3, checked_gpu_glyphs=len(actual), exact_alpha=True,
                glyphs=sorted(glyphs, key=lambda glyph: glyph['scalar']),
                physical_ime='This trace uses controlled SDL commits; physical IME confirmation is separate',
                scope='Current native single-line GUI font; single fixed page; complex shaping and multi-page rendering remain pending')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('cpu', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--player', action='store_true', help='Check the player font resource and loader evidence')
    args = parser.parse_args()
    report = verify(args.trace, args.cpu, font='gfx/fonts/eu4-unicode/zh-hans-16' if args.player else 'gfx/fonts/zh-hans-16')
    if args.player:
        records = [json.loads(line) for line in args.trace.read_text(encoding='utf-8').splitlines() if line]
        artifact = next(record for record in records if record.get('event') == 'artifact')
        loader = [record['payload'] for record in records if record.get('payload', {}).get('event') == 'player-loader']
        if artifact.get('distribution') != 'player overlay' or artifact.get('enabled_mods') != [] or len(loader) != 1 or loader[0].get('legacy_loaded') is not False or loader[0].get('menu_patch_loaded') is not True:
            raise ValueError('Player overlay/vanilla/legacy conflict/MenuPatch evidence incomplete')
        for key in ['loader_sha256', 'vanilla_core_sha256']:
            if not re.fullmatch('[0-9a-f]{64}', artifact.get(key, '')):
                raise ValueError('Missing player resource fingerprint')
        report.update(distribution='player overlay', loader_sha256=artifact['loader_sha256'],
                      vanilla_core_sha256=artifact['vanilla_core_sha256'], enabled_mods=[],
                      legacy_loaded=False, menu_patch_loaded=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('PASS: three complete native commits and 14 exact GPU glyph metric/alpha regions; no ellipsis placeholders.')


if __name__ == '__main__':
    main()
