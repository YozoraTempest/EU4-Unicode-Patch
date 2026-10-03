"""Reject invalid native copy/cut-to-paste roundtrip evidence."""
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


class RoundtripEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.path = ROOT / 'tests/evidence/native-clipboard-roundtrip.jsonl'
        self.records = [json.loads(line) for line in self.path.read_text(encoding='utf-8').splitlines()]

    def result(self, name='copy-supplementary'):
        return next(r['payload'] for r in self.records
                    if r.get('payload', {}).get('event') == 'clipboard-case'
                    and r['payload']['name'] == name)

    def rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text('\n'.join(json.dumps(r) for r in self.records), encoding='utf-8')
            with self.assertRaises(ValueError):
                module.verify(path, roundtrip=True)

    def test_valid_evidence(self):
        self.assertTrue(module.verify(self.path, roundtrip=True)['passed'])

    def test_copy_changed_selection(self):
        self.result()['action_result']['after']['active'] = False
        self.rejected()

    def test_cut_removed_wrong_range(self):
        self.result('cut-supplementary')['action_result']['after']['text'] = [65, 240, 90]
        self.rejected()

    def test_copy_corrupted_source(self):
        self.result()['action_result']['writes'] = [[0xf0, 0xa0]]
        self.rejected()

    def test_copy_read_clipboard(self):
        self.result()['action_result']['reads'] = 1
        self.rejected()

    def test_duplicate_write(self):
        action = self.result()['action_result']
        action['writes'].append(action['writes'][0])
        self.rejected()

    def test_paste_skipped_actual_cut_state(self):
        self.result('cut-supplementary')['paste_before'] = self.result('cut-supplementary')['before']
        self.rejected()

    def test_partial_paste(self):
        self.result()['inserts'] = [[0xf0], [0xa0, 0x80, 0x80]]
        self.rejected()

    def test_missing_final_notification(self):
        self.result()['notifications'] = []
        self.rejected()

    def test_wrong_mode(self):
        next(r for r in self.records if r.get('event') == 'artifact')['mode'] = 'paste'
        self.rejected()


if __name__ == '__main__':
    unittest.main()
