"""Regression gates for cross-platform identity, standalone tools and draft publishing."""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from release_info import asset_names, release_info
from release_bundle import assemble, digest, verify_bundle
import publish_release as publisher


def json_bytes(value):
    return (json.dumps(value, indent=2) + "\n").encode()


def archive(path, files):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.name.endswith(".zip"):
        with zipfile.ZipFile(path, "w") as output:
            for name, content in files.items():
                output.writestr(name, content)
    else:
        with tarfile.open(path, "w:gz") as output:
            for name, content in files.items():
                entry = tarfile.TarInfo(name)
                entry.size = len(content)
                entry.mode = 0o755 if name.endswith(".sh") or name == "diagnostic_probe" else 0o644
                output.addfile(entry, io.BytesIO(content))


def fixture(root, info):
    windows, linux = root / "windows", root / "linux"
    (windows / "build").mkdir(parents=True)
    linux.mkdir()
    pe, loader, elf = b"MZ-patch", b"MZ-loader", b"\x7fELF-patch"
    tests = ["diagnostic_probe", "unicode_layout", "paragraph_transport", "unicode_search", "legacy_text", "yaml_input"]
    xml = '<testsuite tests="6" failures="0" skipped="0">' + ''.join(f'<testcase name="{name}" status="run"/>' for name in tests) + '</testsuite>'
    (windows / "build/ctest-results.xml").write_text(xml)
    (linux / "ctest-linux-results.xml").write_text(xml)
    validation = dict(source_commit=info["SourceCommit"], passed=True, loader_guards=True, optional_fonts=True,
                      ctest=tests, patch_dll_sha256=digest(pe), loader_sha256=digest(loader))
    (windows / "build/automated-validation.json").write_bytes(json_bytes(validation))
    (windows / "build/package-info.json").write_bytes(json_bytes(info))
    native_info = dict(source_commit=info["SourceCommit"], patch_version=info["Version"], release_tag=info["Tag"],
                       patch_sha256=digest(elf), source_tree_dirty=False, excluded_tests="", ctest_sha256=digest(xml.encode()))
    (linux / "linux-build-info.json").write_bytes(json_bytes(native_info))
    archive(windows / "dist" / info["PlayerPackage"], {"VERSION.dll": loader, "plugins/eu4_unicode_patch.dll": pe,
            "plugins/eu4_unicode_patch/LICENSE.txt": b"license", "EU4UnicodePatch.README.txt": info["Tag"].encode()})
    archive(windows / "dist" / info["FontPackage"], {"plugins/eu4_unicode_patch/font-manifest.json": json_bytes(dict(source_commit=info["SourceCommit"], patch_version=info["Version"]))})
    native = {"libeu4_unicode.so": elf, "build-info.json": json_bytes(native_info), "launch-linux.sh": b"#!/bin/sh\n"}
    native["SHA256SUMS"] = ''.join(f'{digest(content)}  {name}\n' for name, content in native.items()).encode()
    native_archive = linux / f"EU4UnicodePatch-linux-x86_64-v{info['Version']}.tar.gz"
    archive(native_archive, native)
    (linux / "linux-package-sha256.txt").write_text(digest(native_archive.read_bytes()) + '  ' + native_archive.name + '\n')
    for platform, directory, key, patchhash in [("windows", windows / "dist", "WindowsDiagnostics", digest(pe)), ("linux", linux, "LinuxDiagnostics", digest(elf))]:
        files = {"README.txt": b"independent diagnostic", "LICENSE": b"license"}
        names = ["diagnose-windows.cmd", "diagnose-windows.ps1", "diagnostic_probe.exe", "executable_check.exe"] if platform == "windows" else ["diagnose-linux.sh", "diagnose-linux.py", "diagnostic_probe"]
        files.update({name: b"tool" for name in names})
        metadata = dict(source_commit=info["SourceCommit"], patch_version=info["Version"], release_tag=info["Tag"],
                        platform=platform, patch_sha256=patchhash, source_tree_dirty=False, files={name:digest(data) for name,data in files.items()})
        if platform == "windows":
            metadata["loader_sha256"] = digest(loader)
        files["diagnostic-info.json"] = json_bytes(metadata)
        archive(directory / info[key], files)
    return windows, linux


class FakeGitHub:
    def __init__(self, info, directory, corrupt_upload=False):
        self.info, self.directory = info, directory
        self.release = None
        self.calls = []
        self.corrupt_upload = corrupt_upload
    def get(self, endpoint, optional=False):
        if endpoint.startswith("git/ref/"):
            if self.release and not self.release["draft"]:
                return {"object": {"type":"commit", "sha":self.info["SourceCommit"]}}
            return None
        if endpoint == "releases/latest":
            return None
        return copy.deepcopy(self.release)
    def run(self, *arguments):
        self.calls.append(tuple(map(str, arguments)))
        action = arguments[1]
        if action == "create":
            self.release = dict(draft=True, prerelease=self.info["Channel"] == "nightly", tag_name=self.info["Tag"], assets=[])
        elif action == "upload":
            self.release["assets"] = [dict(name=path.name, size=path.stat().st_size, state="uploaded", digest="sha256:" + digest(path.read_bytes())) for path in self.directory.iterdir()]
            if self.corrupt_upload:
                self.release["assets"][0]["digest"] = "sha256:" + "0"*64
        elif action == "edit" and "--draft=false" in arguments:
            self.release["draft"] = False
        elif action == "download":
            return (self.directory / arguments[arguments.index("--pattern")+1]).read_text()
        return ""


class PipelineTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.info = release_info("Nightly", "20261020")
        self.windows, self.linux = fixture(self.root, self.info)
        self.output = self.root / "bundle"
    def build(self):
        return assemble(self.windows, self.linux, self.output, self.info)
    def test_complete_bundle_separates_platform_tools(self):
        result = self.build()
        self.assertEqual(len(result["assets"]), 5)
        self.assertEqual(len(list(self.output.iterdir())), 7)
        verify_bundle(self.output, self.info)
    def test_mixed_commit_rejected_before_output(self):
        path = self.linux / "linux-build-info.json"
        data = json.loads(path.read_text());data["source_commit"] = "0"*40;path.write_bytes(json_bytes(data))
        with self.assertRaisesRegex(ValueError, "identity"):
            self.build()
        self.assertFalse(self.output.exists())
    def test_dirty_linux_build_rejected(self):
        path = self.linux / "linux-build-info.json"
        data = json.loads(path.read_text());data["source_tree_dirty"] = True;path.write_bytes(json_bytes(data))
        with self.assertRaises(ValueError):
            self.build()
    def test_skipped_suite_rejected(self):
        (self.linux / "ctest-linux-results.xml").write_text('<testsuite skipped="1"><testcase name="unicode_layout"><skipped/></testcase></testsuite>')
        with self.assertRaises(ValueError):
            self.build()
    def test_missing_platform_diagnostic_rejected(self):
        (self.windows / "dist" / self.info["WindowsDiagnostics"]).unlink()
        with self.assertRaises(FileNotFoundError):
            self.build()
    def test_modified_archive_cannot_pass_final_gate(self):
        self.build()
        (self.output / self.info["LinuxPackage"]).write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "checksum"):
            verify_bundle(self.output, self.info)
    def test_partial_upload_stays_draft(self):
        self.build();client = FakeGitHub(self.info, self.output, True)
        with patch.object(publisher.subprocess, "check_output", return_value=""):
            with self.assertRaisesRegex(ValueError, "digest"):
                publisher.publish(self.info, self.output, client)
        self.assertTrue(client.release["draft"])
        self.assertFalse(any("--draft=false" in call for call in client.calls))
    def test_verified_draft_publishes_once_and_rerun_reads_only(self):
        self.build();client = FakeGitHub(self.info, self.output)
        with patch.object(publisher.subprocess, "check_output", return_value=""):
            self.assertFalse(publisher.publish(self.info, self.output, client))
        self.assertFalse(client.release["draft"])
        client.calls.clear()
        self.assertTrue(publisher.publish(self.info, self.output, client, True))
        self.assertTrue(all(call[1] == "download" for call in client.calls))
    def test_preflight_does_not_create_a_release(self):
        client = FakeGitHub(self.info, self.output)
        self.assertFalse(publisher.publish(self.info, self.output, client, True))
        self.assertIsNone(client.release)
        self.assertEqual(client.calls, [])
    def test_release_identity_is_shared_and_dates_are_validated(self):
        release = release_info("Release")
        self.assertEqual(release["PlayerPackage"], f"EU4UnicodePatch-1.37.5-v{release['Version']}.zip")
        self.assertIn(self.info["SourceCommit"][:7], self.info["LinuxDiagnostics"])
        with self.assertRaises(ValueError):
            release_info("Nightly", "20260230")
        with self.assertRaises(ValueError):
            release_info("Release", "20261020")


if __name__ == "__main__":
    unittest.main()
