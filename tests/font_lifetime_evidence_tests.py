"""Reject leaked, stale, corrupted or incomplete native font lifetime records."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('font_verify', ROOT / 'tools/verify-font-lifetime.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.records = [json.loads(line) for line in
                        (ROOT / 'tests/evidence/native-font-lifetime.jsonl').read_text(encoding='utf-8').splitlines()]

    def cycle(self):
        return next(r['payload'] for r in self.records if r.get('payload', {}).get('event') == 'native-font-lifecycle')

    def rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text('\n'.join(json.dumps(r) for r in self.records), encoding='utf-8')
            with self.assertRaises(ValueError):
                module.verify(path)

    def test_native_baseline(self):
        self.assertTrue(module.verify(ROOT / 'tests/evidence/native-font-lifetime.jsonl')['passed'])

    def test_reachable_dead_atlas(self):
        self.cycle()['after_destroy_missing']['131072'] = False
        self.rejected()

    def test_record_leak(self):
        self.cycle()['after_usage']['glyphs'] += 1
        self.rejected()

    def test_wrong_generation_metrics(self):
        self.cycle()['metrics']['131072'] = [0]*16
        self.rejected()

    def test_alias_copy_changed(self):
        self.cycle()['alias_same']['131072'] = False
        self.rejected()

    def test_live_font_damaged(self):
        self.cycle()['source_unchanged'] = False
        self.rejected()

    def test_duplicate_cycle(self):
        self.records.append(dict(type='send', payload=self.cycle()))
        self.rejected()

    def test_missing_complete_summary(self):
        self.records = [r for r in self.records if r.get('payload', {}).get('event') != 'font-lifetime-complete']
        self.rejected()

    def test_agent_error(self):
        self.records.append(dict(type='error', description='access violation'))
        self.rejected()


if __name__ == '__main__':
    unittest.main()
