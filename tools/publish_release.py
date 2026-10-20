"""Publish a verified cross-platform bundle through a draft release."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import quote
from release_info import ROOT, asset_names, release_info
from release_bundle import digest, verify_bundle

REPOSITORY = "YozoraTempest/EU4-Unicode-Patch"


class GitHub:
    def run(self, *arguments):
        result = subprocess.run(["gh", *map(str, arguments)], capture_output=True, text=True, encoding="utf-8", errors="replace")
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or result.stdout.strip())
        return result.stdout

    def get(self, endpoint, optional=False):
        try:
            return json.loads(self.run("api", f"repos/{REPOSITORY}/{endpoint}"))
        except RuntimeError as error:
            if optional and "(HTTP 404)" in str(error):
                return None
            raise


def check_tag(client, info, required=False):
    tag = client.get("git/ref/tags/" + quote(info["Tag"], safe=""), optional=True)
    if tag is None:
        if required:
            raise ValueError("Published release tag is missing")
        return
    target = tag["object"]
    while target["type"] == "tag":
        target = client.get("git/tags/" + target["sha"])["object"]
    if target["type"] != "commit" or target["sha"] != info["SourceCommit"]:
        raise ValueError("Release tag points to a different source commit")


def check_remote_assets(release, info, inventory):
    assets = {asset["name"]: asset for asset in release["assets"]}
    if set(assets) != set(inventory) or len(release["assets"]) != len(inventory):
        raise ValueError("Remote release has missing, duplicate or unexpected assets")
    for name, expected in inventory.items():
        actual = assets[name]
        if actual.get("state") != "uploaded" or actual.get("size") != expected["bytes"] or actual.get("digest") != "sha256:" + expected["sha256"]:
            raise ValueError("Remote uploaded digest/size mismatch: " + name)
    if release.get("tag_name") != info["Tag"]:
        raise ValueError("Remote release tag mismatch")


def existing_inventory(client, release, info):
    text = client.run("release", "download", info["Tag"], "--repo", REPOSITORY, "--pattern", "release-manifest.json", "--output", "-")
    manifest = json.loads(text)
    if manifest.get("source_commit") != info["SourceCommit"] or manifest.get("release_tag") != info["Tag"] or manifest.get("channel") != info["Channel"] or manifest.get("patch_version") != info["Version"]:
        raise ValueError("Published manifest belongs to another source/channel")
    if set(manifest["assets"]) != set(asset_names(info)):
        raise ValueError("Published manifest asset set mismatch")
    checksum_text = client.run("release", "download", info["Tag"], "--repo", REPOSITORY, "--pattern", "SHA256SUMS.txt", "--output", "-")
    checksums = {}
    for line in checksum_text.splitlines():
        hashvalue, name = line.split("  ", 1)
        if name in checksums:
            raise ValueError("Duplicate published checksum")
        checksums[name] = hashvalue
    if set(checksums) != set(asset_names(info)) | {"release-manifest.json"} or checksums["release-manifest.json"] != digest(text.encode("utf-8")):
        raise ValueError("Published checksum inventory is invalid")
    if any(checksums[name] != row["sha256"] for name, row in manifest["assets"].items()):
        raise ValueError("Published checksums differ from manifest")
    inventory = dict(manifest["assets"])
    inventory["release-manifest.json"] = dict(sha256=digest(text.encode("utf-8")), bytes=len(text.encode("utf-8")))
    inventory["SHA256SUMS.txt"] = dict(sha256=digest(checksum_text.encode("utf-8")), bytes=len(checksum_text.encode("utf-8")))
    check_remote_assets(release, info, inventory)


def release_notes(info):
    changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    section_name = "未发布" if info["Channel"] == "nightly" else info["Version"]
    section = re.search(r"(?ms)^## " + re.escape(section_name) + r"\s*\n(.*?)(?=^## |\Z)", changelog)
    if not section:
        raise ValueError("Add a changelog section before publishing: " + section_name)
    prefix = f"Nightly 测试版，构建日期：{info['BuildDate']}（北京时间）。\n\n" if info["Channel"] == "nightly" else ""
    return prefix + section.group(1).strip() + f"""\n
适用：EU4 1.37.5，Windows x64 / Linux x86_64。

Windows：将 `{info['PlayerPackage']}` 解压到 eu4.exe 所在目录；需要补字时安装可选字体包。
Linux：解压 `{info['LinuxPackage']}`，按 README 运行一次 install-paradox-launcher.sh，保留 P 社启动器，Steam 启动选项可留空。

诊断工具独立下载：Windows 使用 `{info['WindowsDiagnostics']}`；Linux 使用 `{info['LinuxDiagnostics']}`。不需要复制到游戏目录，不包含在补丁包内。
全部附件的源码提交与哈希见 release-manifest.json 和 SHA256SUMS.txt。
自动测试和运行库验证不代表全部真实桌面、输入法、GPU 与战役已经验收。

提交：[{info['SourceCommit'][:7]}](https://github.com/{REPOSITORY}/commit/{info['SourceCommit']})
"""


def publish(info, directory, client, check_only=False):
    check_tag(client, info)
    release = client.get("releases/tags/" + quote(info["Tag"], safe=""), optional=True)
    if release and not release["draft"]:
        if bool(release["prerelease"]) != (info["Channel"] == "nightly"):
            raise ValueError("Existing release has the wrong channel")
        check_tag(client, info, required=True)
        existing_inventory(client, release, info)
        return True
    if info["Channel"] == "release":
        latest = client.get("releases/latest", optional=True)
        if latest and re.fullmatch(r"v\d+\.\d+\.\d+", latest["tag_name"]):
            if tuple(map(int, info["Version"].split("."))) <= tuple(map(int, latest["tag_name"][1:].split("."))):
                raise ValueError("Formal release version must increase")
    if check_only:
        return False
    if subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"], text=True).strip():
        raise ValueError("Commit source changes before publishing")
    verify_bundle(directory, info)
    # Generate notes outside the exact asset directory checked above.
    notes_path = directory.parent / "release-notes.md"
    notes_path.write_text(release_notes(info), encoding="utf-8")
    names = set(asset_names(info)) | {"SHA256SUMS.txt", "release-manifest.json"}
    if release and any(asset["name"] not in names for asset in release["assets"]):
        raise ValueError("Draft contains unexpected assets; review it before retrying")
    if release is None:
        options = ["release", "create", info["Tag"], "--repo", REPOSITORY, "--target", info["SourceCommit"], "--draft", "--title", "EU4 Unicode Patch " + info["Tag"], "--notes-file", notes_path, "--latest=false"]
        if info["Channel"] == "nightly":
            options.append("--prerelease")
        client.run(*options)
    else:
        client.run("release", "edit", info["Tag"], "--repo", REPOSITORY, "--notes-file", notes_path)
    client.run("release", "upload", info["Tag"], *[directory / name for name in sorted(names)], "--repo", REPOSITORY, "--clobber")
    uploaded = client.get("releases/tags/" + quote(info["Tag"], safe=""))
    inventory = {name: dict(sha256=digest((directory / name).read_bytes()), bytes=(directory / name).stat().st_size) for name in names}
    check_remote_assets(uploaded, info, inventory)
    client.run("release", "edit", info["Tag"], "--repo", REPOSITORY, "--draft=false",
               "--prerelease" if info["Channel"] == "nightly" else "--prerelease=false",
               "--latest=false" if info["Channel"] == "nightly" else "--latest")
    final = client.get("releases/tags/" + quote(info["Tag"], safe=""))
    if final["draft"] or bool(final["prerelease"]) != (info["Channel"] == "nightly"):
        raise ValueError("Release publication state is unconfirmed")
    check_remote_assets(final, info, inventory)
    check_tag(client, info, required=True)
    return False


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--channel", choices=("Release", "Nightly"), required=True)
    parser.add_argument("--build-date", default="")
    parser.add_argument("--bundle-directory", type=Path, default=ROOT / "build/release-bundle")
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    info = release_info(args.channel, args.build_date)
    skip = publish(info, args.bundle_directory.resolve(), GitHub(), args.check_only)
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
            stream.write(f"skip={str(skip).lower()}\n")
    print(("Already published: " if skip else "Preflight passed: " if args.check_only else "Published: ") + info["Tag"])
