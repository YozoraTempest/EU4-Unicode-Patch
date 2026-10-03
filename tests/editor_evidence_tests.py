"""Reject false positive or incomplete native editor evidence."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('editor_verify', ROOT / 'tools/verify-editor-trace.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.records = [json.loads(line) for line in
                        (ROOT / 'tests/evidence/native-editor-trace.jsonl').read_text(encoding='utf-8').splitlines()]

    def rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text('\n'.join(json.dumps(r) for r in self.records), encoding='utf-8')
            with self.assertRaises(ValueError):
                module.verify(path)

    def test_wrong_text_cannot_be_accepted_by_passed_flag(self):
        record = next(r['payload'] for r in self.records if r.get('payload', {}).get('event') == 'native-editor-key')
        record['after'] = 'A中𠀀Z'
        self.rejected()

    def test_missing_budget_case(self):
        self.records = [r for r in self.records if r.get('payload', {}).get('name') != 'zwj-budget']
        self.rejected()

    def test_agent_error(self):
        self.records.append({'type': 'error', 'description': 'access violation'})
        self.rejected()

    def test_duplicated_case(self):
        self.records.append(next(r for r in self.records if r.get('payload', {}).get('event') == 'native-editor-filter'))
        self.rejected()


if __name__ == '__main__':
    unittest.main()
