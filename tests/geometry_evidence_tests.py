"""Reject incomplete or misaligned native pixel evidence."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('geometry_verify', ROOT / 'tools/verify-editor-geometry.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.records = [json.loads(line) for line in
                        (ROOT / 'tests/evidence/native-editor-geometry.jsonl').read_text(encoding='utf-8').splitlines()]

    def case(self, name='supplementary'):
        return next(r['payload'] for r in self.records if r.get('payload', {}).get('name') == name)

    def rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text('\n'.join(json.dumps(r) for r in self.records), encoding='utf-8')
            with self.assertRaises(ValueError):
                module.verify(path)

    def test_valid_native_baseline(self):
        self.assertTrue(module.verify(ROOT / 'tests/evidence/native-editor-geometry.jsonl')['passed'])

    def test_interior_caret(self):
        self.case()['hits'][5][1] = 3
        self.rejected()

    def test_wrong_nearest_edge(self):
        self.case()['hits'][5][1] = 8
        self.rejected()

    def test_partial_fit(self):
        self.case()['fits'][0][1] = 3
        self.rejected()

    def test_no_progress(self):
        self.case()['fits'][0][1] = 0
        self.rejected()

    def test_partial_combining_space(self):
        self.case('space-combining')['words'][0][1] = 1
        self.rejected()

    def test_incomplete_pixel_range(self):
        self.case()['hits'].pop()
        self.rejected()

    def test_missing_completion(self):
        self.records = [r for r in self.records if r.get('payload', {}).get('event') != 'geometry-complete']
        self.rejected()

    def test_duplicate_case(self):
        self.records.append(dict(type='send', payload=self.case()))
        self.rejected()

    def test_wrong_font_contract(self):
        next(r['payload'] for r in self.records if r.get('payload', {}).get('event') == 'geometry-font')['measure_rva'] = '0x159b4d0'
        self.rejected()

    def test_agent_error(self):
        self.records.append(dict(type='error', description='access violation'))
        self.rejected()


if __name__ == '__main__':
    unittest.main()
