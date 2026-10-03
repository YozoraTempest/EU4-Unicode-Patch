"""Reject clipboard evidence with broken ownership, text or native selection."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('clipboard_verify', ROOT / 'tools/verify-clipboard.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.path = ROOT / 'tests/evidence/native-clipboard.jsonl'
        self.records = [json.loads(line) for line in self.path.read_text(encoding='utf-8').splitlines()]

    def result(self, name='supplementary'):
        return next(r['payload'] for r in self.records
                    if r.get('payload', {}).get('event') == 'clipboard-case'
                    and r['payload']['name'] == name)

    def rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text('\n'.join(json.dumps(r) for r in self.records), encoding='utf-8')
            with self.assertRaises(ValueError):
                module.verify(path)

    def test_valid_evidence(self):
        self.assertTrue(module.verify(self.path)['passed'])

    def test_missing_release(self):
        self.result()['releases'] = 0
        self.rejected()

    def test_duplicate_release(self):
        self.result()['releases'] = 2
        self.rejected()

    def test_missing_observer_map(self):
        next(r for r in self.records if r.get('event') == 'artifact').pop('map_sha256')
        self.rejected()

    def test_partial_insert(self):
        self.result()['inserts'] = [[0xf0], [0xa0], [0x80], [0x80]]
        self.rejected()

    def test_incomplete_notification(self):
        self.result()['notifications'] = [[0xf0, 0xa0]]
        self.rejected()

    def test_selection_not_replaced(self):
        self.result('selected-supplementary')['after']['text'] = list('A中𠀀中文Z'.encode('utf-8'))
        self.rejected()

    def test_blocked_input_cleared_selection(self):
        self.result('blocked-preserves-selection')['after']['active'] = False
        self.rejected()

    def test_invalid_input_was_inserted(self):
        self.result('invalid-preserves-selection')['inserts'] = [[0xed, 0xa0, 0x80]]
        self.rejected()

    def test_font_filter_corrupted_scalar(self):
        self.result('font-filter-continuation')['after']['text'] = [0xe4, 0xbf]
        self.rejected()

    def test_missing_case(self):
        target = self.result()
        self.records = [r for r in self.records if r.get('payload') is not target]
        self.rejected()

    def test_missing_summary(self):
        self.records = [r for r in self.records if r.get('payload', {}).get('event') != 'clipboard-complete']
        self.rejected()

    def test_agent_error(self):
        self.records.append(dict(type='error', description='native access violation'))
        self.rejected()


if __name__ == '__main__':
    unittest.main()
