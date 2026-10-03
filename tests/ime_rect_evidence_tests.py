"""Reject incomplete or incorrect native caret-to-SDL rectangle evidence."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('ime_rect_verify', ROOT / 'tools/verify-ime-rect.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ImeRectEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.path = ROOT / 'tests/evidence/native-ime-rect.jsonl'
        self.records = [json.loads(line) for line in self.path.read_text(encoding='utf-8').splitlines()]

    def event(self, name, case=None):
        return next(r['payload'] for r in self.records if r.get('payload', {}).get('event') == name
                    and (case is None or r['payload'].get('name') == case))

    def rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text('\n'.join(json.dumps(r) for r in self.records), encoding='utf-8')
            with self.assertRaises(ValueError):
                module.verify(path)

    def test_valid_evidence(self):
        self.assertTrue(module.verify(self.path)['passed'])

    def test_partial_utf8_source(self):
        self.event('ime-rect-case', 'supplementary')['source']['text'] = [65, 240, 160, 90]
        self.rejected()

    def test_rectangle_at_byte_offset(self):
        self.event('ime-rect-case', 'supplementary')['rect'][0] = 333
        self.rejected()

    def test_duplicate_stable_call(self):
        self.event('ime-rect-case', 'empty')['calls'] = [dict(rect=[328, 485, 1, 16], focused=True)]
        self.rejected()

    def test_unfocused_update(self):
        self.event('ime-rect-unfocused')['calls'] = [dict(rect=[392, 485, 1, 16], focused=False)]
        self.rejected()

    def test_stale_focus_transform(self):
        call = self.event('sdl-ime-rect')
        call['rect'] = [640, 328, 1, 16]
        call['source']['sprite'] = [640, 328]
        self.rejected()

    def test_api_geometry_mismatch(self):
        call = next(r['payload'] for r in self.records if r.get('payload', {}).get('event') == 'sdl-ime-rect'
                    and r['payload']['rect'][0] == 357)
        call['source']['sprite'][0] = 358
        self.rejected()

    def test_wrong_call_thread(self):
        calls = [r['payload'] for r in self.records if r.get('payload', {}).get('event') == 'sdl-ime-rect']
        calls[-1]['thread'] += 1
        self.rejected()

    def test_missing_refocus_invalidation(self):
        self.event('ime-rect-refocused')['calls'] = []
        self.rejected()

    def test_missing_completed_api_call(self):
        self.records.remove(next(r for r in self.records if r.get('payload', {}).get('event') == 'sdl-ime-rect'))
        self.rejected()

    def test_other_editor_owned_call(self):
        calls = [r['payload'] for r in self.records if r.get('payload', {}).get('event') == 'sdl-ime-rect']
        calls[-1]['owner'] = '0x12345678'
        self.rejected()

    def test_reordered_focus_evidence(self):
        left = next(i for i, r in enumerate(self.records) if r.get('payload', {}).get('event') == 'ime-rect-unfocused')
        right = next(i for i, r in enumerate(self.records) if r.get('payload', {}).get('event') == 'ime-rect-refocused')
        self.records[left], self.records[right] = self.records[right], self.records[left]
        self.rejected()

    def test_agent_error(self):
        self.records.append(dict(type='error', description='Fixture error'))
        self.rejected()


if __name__ == '__main__':
    unittest.main()
