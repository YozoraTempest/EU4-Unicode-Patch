"""Reject false positives in native Windows IME candidate evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('verify_ime_candidates', ROOT / 'tools/verify-ime-candidates.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CandidateEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.records = [json.loads(line) for line in (ROOT / 'tests/evidence/native-ime-candidate-contract.jsonl').read_text(encoding='utf-8').splitlines()]

    def event(self, name):
        return next(r['payload'] for r in self.records[1:] if r.get('payload', {}).get('event') == name)

    def rejected(self):
        with self.assertRaises(ValueError):
            module.verify(self.records)

    def test_complete_evidence(self):
        report = module.verify(self.records)
        self.assertEqual(report['native_context_cases'], 7)
        self.assertEqual(report['windows_candidate_rectangles'], 17)
        self.assertEqual(report['physical_candidate_visibility'], 'pending user verification')

    def test_context_flags_cleared(self):
        case = next(r['payload'] for r in self.records if r.get('payload', {}).get('incoming') == 15 and r['payload']['event'] == 'ime-policy-case')
        case['outgoing'] = 0
        self.rejected()

    def test_context_trapped(self):
        self.event('ime-policy-case')['trapped'] = 1
        self.rejected()

    def test_missing_disabled_context(self):
        self.records = [r for r in self.records if not (r.get('payload', {}).get('event') == 'ime-policy-case' and r['payload'].get('offset') == 0x50)]
        self.rejected()

    def test_rejected_windows_api(self):
        self.event('ime-candidate-request')['accepted'] = False
        self.rejected()

    def test_missing_candidate_request(self):
        self.records = [r for r in self.records if r.get('payload', {}).get('event') != 'ime-candidate-request']
        self.rejected()

    def test_wrong_exclusion_mode(self):
        self.event('ime-candidate-state')['form']['style'] = 0x20
        self.rejected()

    def test_wrong_stored_position(self):
        self.event('ime-candidate-state')['form']['position'][0] += 1
        self.rejected()

    def test_wrong_requested_area(self):
        self.event('ime-candidate-request')['form']['area'][3] += 16
        self.rejected()

    def test_failed_candidate_readback(self):
        self.event('ime-candidate-state')['available'] = False
        self.rejected()

    def test_duplicate_completion(self):
        self.records.append(dict(type='send', payload=copy.deepcopy(self.event('ime-policy-complete'))))
        self.rejected()

    def test_script_error(self):
        self.records.append(dict(type='error', description='Observer failed'))
        self.rejected()


if __name__ == '__main__':
    unittest.main()
