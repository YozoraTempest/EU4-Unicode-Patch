"""Reject saves whose matching strings are outside the expected native fields."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("persistence", ROOT / "tools/verify-persistence-save.py")
persistence = importlib.util.module_from_spec(spec)
spec.loader.exec_module(persistence)

# Minimal native record shapes, including an unrelated legacy CP1252 byte.
DATA = ('EU4txt\nlegacy="é"\n'.encode("cp1252") + '''-183={
		owner="FRA"
		name="巴黎𠀀"
		capital="中文😀"
	}
	FRA={
		army={
			name="中文𠀀测试军"
		}
		navy={
			name="中文😀测试舰队"
		}
		flags={
			eu4_unicode_persistence_initialized=1444.11.11
		}
	}
'''.encode("utf-8"))


class PersistenceTests(unittest.TestCase):
    def check(self, data, compressed=False):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "持久化𠀀.eu4"
            if compressed:
                with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
                    archive.writestr("gamestate", data)
            else:
                path.write_bytes(data)
            return persistence.verify(path)

    def test_native_plaintext_and_compressed_records(self):
        for compressed in [False, True]:
            result = self.check(DATA, compressed)
            self.assertEqual(result["format"], "compressed" if compressed else "plaintext")
            self.assertTrue(result["native_records_checked"])

    def test_strings_in_unrelated_fields_do_not_prove_roundtrip(self):
        for field in [b"name", b"capital", b"army", b"navy"]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.check(DATA.replace(field + b"=", b"unrelated="))

    def test_initialization_and_capital_ownership_are_required(self):
        for data in [DATA.replace(b"eu4_unicode_persistence_initialized=", b"unrelated="),
                     DATA.replace(b'owner="FRA"', b'owner="ENG"')]:
            with self.assertRaises(ValueError):
                self.check(data)

    def test_binary_save_is_not_misidentified_as_text(self):
        with self.assertRaises(ValueError):
            self.check(DATA.replace(b"EU4txt", b"EU4bin"), compressed=True)


if __name__ == "__main__":
    unittest.main()
