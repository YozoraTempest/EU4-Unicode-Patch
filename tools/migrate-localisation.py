"""Convert an explicitly selected EU4dll escaped localization copy to UTF-8.

The outer file encoding is UTF-8; payload bytes are represented with CP1252
characters. The legacy protocol is documented in docs/migration.md.
"""
import argparse
import hashlib
import json
from pathlib import Path

SHIFTS = {0x10: 0, 0x11: -0xE, 0x12: 0x900, 0x13: 0x8F2}


def payload_byte(character):
    try:
        encoded = character.encode("cp1252")
    except UnicodeEncodeError:
        # Windows maps the five undefined CP1252 bytes to their C1 controls.
        if ord(character) in {0x81, 0x8D, 0x8F, 0x90, 0x9D}:
            return ord(character)
        raise ValueError(f"Not a legacy payload byte: U+{ord(character):04X}") from None
    return encoded[0]


def decode_legacy(text):
    result = []
    index = sequences = relocated = 0
    while index < len(text):
        marker = ord(text[index])
        if marker not in SHIFTS:
            result.append(text[index])
            index += 1
            continue
        if index + 2 >= len(text):
            raise ValueError(f"Truncated legacy sequence at character {index}")
        low, high = map(payload_byte, text[index + 1:index + 3])
        scalar = (high << 8) + low + SHIFTS[marker]
        if not 0x100 <= scalar <= 0xFFFF:
            raise ValueError(f"Invalid legacy UTF-16 unit at character {index}")
        if 0xE100 < scalar < 0xEA00:
            scalar -= 0xE000
            relocated += 1
        result.append(chr(scalar))
        sequences += 1
        index += 3
    # Legacy UCS-2/UTF-16 encoders can emit surrogate pairs. Combine valid pairs
    # and reject isolated surrogates before any destination files are created.
    decoded = "".join(result).encode("utf-16-le", "surrogatepass").decode("utf-16-le")
    return decoded, {"sequences": sequences, "relocated": relocated}


def convert_directory(source, destination):
    source, destination = source.resolve(strict=True), destination.resolve()
    if not source.is_dir() or destination == source or source in destination.parents or destination in source.parents:
        raise ValueError("Source and destination must be separate localization directories")
    if destination.exists():
        raise ValueError("Destination already exists; select a new copy directory")
    files = sorted(source.rglob("*.yml"))
    if not files:
        raise ValueError("No localization files found")
    prepared, records = [], []
    for path in files:
        if path.is_symlink() or source not in path.resolve(strict=True).parents:
            raise ValueError("Localization links outside the source are not supported")
        raw = path.read_bytes()
        text = raw.decode("utf-8-sig")
        decoded, counts = decode_legacy(text)
        output = (b"\xef\xbb\xbf" if raw.startswith(b"\xef\xbb\xbf") else b"") + decoded.encode("utf-8")
        relative = path.relative_to(source)
        prepared.append((relative, output))
        records.append({"file": relative.as_posix(), "source_sha256": hashlib.sha256(raw).hexdigest(),
                        "utf8_sha256": hashlib.sha256(output).hexdigest(), **counts})
    # All files are validated first. The source is never written or renamed.
    destination.mkdir(parents=True, exist_ok=False)
    for relative, output in prepared:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(output)
    report = {"format": "EU4dll-escaped-CP1252-in-UTF8", "files": len(records),
              "sequences": sum(record["sequences"] for record in records),
              "relocated": sum(record["relocated"] for record in records), "records": records}
    (destination / "unicode-migration.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--format", choices=["eu4dll-escaped"], required=True,
                        help="Select the audited source protocol explicitly; no encoding guessing")
    args = parser.parse_args()
    report = convert_directory(args.source, args.destination)
    print(json.dumps({key: report[key] for key in ["format", "files", "sequences", "relocated"]}, ensure_ascii=True))


if __name__ == "__main__":
    main()
