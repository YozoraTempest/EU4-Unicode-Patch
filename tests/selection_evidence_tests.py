"""Ensure selection evidence cannot pass with broken or incomplete native state."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('selection_verify', ROOT / 'tools/verify-selection.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.records = [json.loads(line) for line in
                        (ROOT / 'docs/evidence/native-selection.jsonl').read_text(encoding='utf-8').splitlines()]

    def step(self):
        return next(r['payload'] for r in self.records if r.get('payload', {}).get('event') == 'selection-step')

    def rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text('\n'.join(json.dumps(r) for r in self.records), encoding='utf-8')
            with self.assertRaises(ValueError):
                module.verify(path)

    def test_valid_native_baseline(self):
        self.assertTrue(module.verify(ROOT / 'docs/evidence/native-selection.jsonl')['passed'])

    def test_partial_selection(self):
        self.step()['after']['selected'] = [0xf0, 0xa0]
        self.rejected()

    def test_interior_caret(self):
        self.step()['after']['caret'] = 6
        self.rejected()

    def test_changed_anchor(self):
        self.step()['after']['anchor'] = 4
        self.rejected()

    def test_partial_notification(self):
        self.step()['notifications'].append([0xf0, 0xa0])
        self.rejected()

    def test_unhandled_key(self):
        self.step()['handled'] = 0
        self.rejected()

    def test_missing_completion(self):
        self.records = [r for r in self.records if r.get('payload', {}).get('event') != 'selection-validation-complete']
        self.rejected()

    def test_duplicate_step(self):
        self.records.append(dict(type='send', payload=self.step()))
        self.rejected()

    def test_agent_error(self):
        self.records.append(dict(type='error', description='access violation'))
        self.rejected()


if __name__ == '__main__':
    unittest.main()
