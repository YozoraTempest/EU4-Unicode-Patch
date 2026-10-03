"""Reject incomplete, repeated or non-atomic SDL integration evidence."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('sdl_verify', ROOT / 'tools/verify-sdl-input.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.records = [json.loads(line) for line in
                        (ROOT / 'docs/evidence/native-sdl-input.jsonl').read_text(encoding='utf-8').splitlines()]

    def result(self):
        return next(r['payload'] for r in self.records
                    if r.get('payload', {}).get('name') == 'supplementary'
                    and r['payload'].get('event') == 'native-sdl-result')

    def rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text('\n'.join(json.dumps(r) for r in self.records), encoding='utf-8')
            with self.assertRaises(ValueError):
                module.verify(path)

    def height_result(self):
        return next(r['payload'] for r in self.records
                    if r.get('payload', {}).get('name') == 'native-height-fitting'
                    and r['payload'].get('event') == 'native-sdl-result')

    def test_valid_evidence(self):
        self.assertTrue(module.verify(ROOT / 'docs/evidence/native-sdl-input.jsonl')['passed'])

    def test_missing_live_geometry(self):
        self.height_result().pop('height_geometry')
        self.rejected()

    def test_incorrect_live_row_cache(self):
        self.height_result()['height_geometry']['rows'][0].append(65)
        self.rejected()

    def test_incorrect_native_measurement(self):
        self.height_result()['height_geometry']['prefix_widths'][12][1] = 150
        self.rejected()

    def test_incorrect_width_contract(self):
        self.height_result()['height_geometry']['width'] = 170
        self.rejected()

    def test_partial_scalar_insert(self):
        self.result()['inserts'] = [[0xf0], [0xa0], [0x80], [0x80]]
        self.rejected()

    def test_invalid_intermediate_notification(self):
        self.result()['notifications'].insert(0, [0xf0, 0xa0])
        self.rejected()

    def test_missing_queue_copy(self):
        self.result()['queue_copies'] = []
        self.rejected()

    def test_duplicate_case(self):
        self.records.append({'type': 'send', 'payload': self.result()})
        self.rejected()

    def test_missing_complete_summary(self):
        self.records = [r for r in self.records if r.get('payload', {}).get('event') != 'sdl-validation-complete']
        self.rejected()

    def test_agent_error(self):
        self.records.append({'type': 'error', 'description': 'access violation'})
        self.rejected()

    def test_selection_omitted(self):
        selected = next(r['payload'] for r in self.records
                        if r.get('payload', {}).get('name') == 'selected-supplementary-replacement'
                        and r['payload'].get('event') == 'native-sdl-result')
        selected.pop('selection_before')
        self.rejected()

    def test_blocked_commit_cleared_selection(self):
        blocked = next(r['payload'] for r in self.records
                       if r.get('payload', {}).get('name') == 'blocked-commit-preserves-selection'
                       and r['payload'].get('event') == 'native-sdl-result')
        blocked['selection_after']['selected'] = []
        self.rejected()


if __name__ == '__main__':
    unittest.main()
