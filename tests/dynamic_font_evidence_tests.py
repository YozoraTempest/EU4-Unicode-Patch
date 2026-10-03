"""Reject false positives in native font texture and UTF-8 commit evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('verify_dynamic_fonts', ROOT/'tools/verify-dynamic-fonts.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DynamicFontEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.records = [json.loads(line) for line in (ROOT/'docs/evidence/native-dynamic-fonts.jsonl').read_text(encoding='utf-8').splitlines()]
        self.cpu = json.loads((ROOT/'docs/evidence/dynamic-font-cpu.json').read_text(encoding='utf-8'))

    def event(self, name):
        return next(record['payload'] for record in self.records if record.get('payload', {}).get('event') == name)

    def verify(self):
        with tempfile.TemporaryDirectory() as directory:
            trace, cpu = Path(directory)/'trace.jsonl', Path(directory)/'cpu.json'
            trace.write_text('\n'.join(json.dumps(record) for record in self.records), encoding='utf-8')
            cpu.write_text(json.dumps(self.cpu), encoding='utf-8')
            return module.verify(trace, cpu)

    def rejected(self):
        with self.assertRaises(ValueError):
            self.verify()

    def test_complete_native_evidence(self):
        report = self.verify()
        self.assertEqual(report['native_commits'], 3)
        self.assertEqual(report['checked_gpu_glyphs'], 14)
        self.assertTrue(report['exact_alpha'])

    def test_changed_gpu_pixel(self):
        pixels = self.event('dynamic-font-gpu')['glyphs'][0]['alpha']
        pixels[0] ^= 1
        self.rejected()

    def test_changed_native_advance(self):
        self.event('dynamic-font-gpu')['glyphs'][0]['metrics'][-1] += 1
        self.rejected()

    def test_placeholder_pointer(self):
        self.event('dynamic-font-gpu')['glyphs'][0]['ellipsis_same'] = True
        self.rejected()

    def test_missing_gpu_glyph(self):
        self.event('dynamic-font-gpu')['glyphs'].pop()
        self.rejected()

    def test_duplicate_gpu_sample(self):
        self.records.append(dict(type='send', payload=copy.deepcopy(self.event('dynamic-font-gpu'))))
        self.rejected()

    def test_failed_gpu_readback(self):
        self.event('dynamic-font-gpu')['glyphs'][0]['operations']['readback'] = -1
        self.rejected()

    def test_wrong_texture_pool(self):
        self.event('dynamic-font-gpu')['pool'] = 2
        self.rejected()

    def test_out_of_atlas(self):
        self.event('dynamic-font-gpu')['glyphs'][0]['metrics'][0] = 2048
        self.rejected()

    def test_truncated_native_queue(self):
        self.event('native-sdl-result')['queue_copies'][0][2] = 0
        self.rejected()

    def test_duplicate_native_notification(self):
        notifications = self.event('native-sdl-result')['notifications']
        notifications.append(copy.deepcopy(notifications[0]))
        self.rejected()

    def test_wrong_caret(self):
        self.event('native-sdl-result')['after_caret'] -= 1
        self.rejected()

    def test_missing_completion(self):
        self.records = [record for record in self.records if record.get('payload', {}).get('event') != 'sdl-validation-complete']
        self.rejected()

    def test_missing_cpu_reference(self):
        self.cpu['glyphs'].pop()
        self.rejected()

    def test_wrong_executable(self):
        self.records[0]['exe_sha256'] = '0'*64
        self.rejected()

    def test_observer_error(self):
        self.records.append(dict(type='error', description='GPU observer failed'))
        self.rejected()


if __name__ == '__main__':
    unittest.main()
