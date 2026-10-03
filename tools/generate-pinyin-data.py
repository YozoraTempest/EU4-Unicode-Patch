"""Generate the pinned, sorted pronunciation table used by the C++ matcher."""
import argparse
import hashlib
import json
from pathlib import Path
import unicodedata


def syllable(text):
    result = []
    for char in unicodedata.normalize("NFD", text.casefold()):
        if char == "\u0308" and result and result[-1] == "u":
            result[-1] = "v"
        elif not unicodedata.combining(char):
            result.append(char)
    value = "".join(result)
    if not value or any(not ("a" <= char <= "z") for char in value):
        raise ValueError(f"Invalid pinyin syllable: {text!r}")
    return value


def generate(source, output):
    manifest = json.loads((source / "source.json").read_text(encoding="utf-8"))
    for name, digest in manifest["sha256"].items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f"Pinned source hash mismatch: {name}")
    entries = {}
    for number, raw in enumerate((source / "pinyin.txt").read_text(encoding="utf-8-sig").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        phrase, pronunciation = line.split(":", 1)
        phrase = unicodedata.normalize("NFKC", phrase.strip()).casefold()
        reading = " ".join(syllable(part) for part in pronunciation.split())
        if len(phrase) != len(reading.split()):
            raise ValueError(f"Pronunciation length mismatch on line {number}")
        alternatives = entries.setdefault(phrase, [])
        if reading not in alternatives:
            alternatives.append(reading)
    lines = ["// Generated from the pinned phrase-pinyin-data source. Do not edit.",
             "static constexpr PhraseEntry builtin_phrases[] = {"]
    for phrase, readings in sorted(entries.items(), key=lambda item: item[0].encode("utf-8")):
        lines.append("    {u8" + json.dumps(phrase, ensure_ascii=False) + "," + json.dumps("|".join(readings)) + "},")
    lines.extend(["};", f"static constexpr std::size_t builtin_max_characters={max(map(len, entries))};", ""])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    generate(args.source, args.output)
