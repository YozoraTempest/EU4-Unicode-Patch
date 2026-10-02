"""Read the dedicated EU4 1.37.5 persistence fixture; never modify its save."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile


def verify(path):
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            damaged = archive.testzip()
            if damaged:
                raise ValueError(f"Damaged compressed save member: {damaged}")
            data = archive.read("gamestate")
        save_format = "compressed"
    else:
        data = path.read_bytes()
        save_format = "plaintext"
    if not data.startswith(b"EU4txt\n"):
        raise ValueError("This verifier accepts EU4txt fixture saves only")
    province = re.search(rb"(?ms)^-183=\{\n(.*?)^\t\}", data)
    if not province or b'owner="FRA"' not in province[1]:
        raise ValueError("The dedicated French capital province is missing")
    expected = {"province": "巴黎𠀀", "capital": "中文😀",
                "army": "中文𠀀测试军", "navy": "中文😀测试舰队"}
    for kind, name in expected.items():
        field = b"capital" if kind == "capital" else b"name"
        token = field + b'="' + name.encode("utf-8") + b'"'
        if kind in {"province", "capital"}:
            found = token in province[1]
        else:
            blocks = re.findall(rb"(?ms)^\t\t" + kind.encode("ascii") + rb"=\{\n(.*?)^\t\t\}", data)
            found = any(token in block for block in blocks)
        if not found:
            raise ValueError(f"UTF-8 name missing from its native {kind} record")
    flag = b"eu4_unicode_persistence_initialized="
    if flag not in data:
        raise ValueError("Initialization flag missing; reloading could hide lost names")
    return {"filename": path.name, "format": save_format, "size": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "names": expected,
            "initialization_flag": True, "native_records_checked": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("save", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    result = verify(args.save)
    if args.report:
        args.report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=True))


if __name__ == "__main__":
    main()
