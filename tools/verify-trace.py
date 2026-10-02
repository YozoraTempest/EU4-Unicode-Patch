"""Check observed localization bytes and glyph events from the isolated game."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
events=[message["payload"] for line in (ROOT/"private/import-trace.jsonl").read_text(encoding="utf-8").splitlines()
        if (message:=json.loads(line)).get("type")=="send"]
expected={
    "FE_SINGLE_PLAYER":"单人游戏 UTF8",
    "FE_MULTI_PLAYER":"中文与 English",
    "EU4_UNICODE_PROBE":"中文与 English 123，法兰西 é ü ß。",
    "EU4_UNICODE_SCALAR_PROBE":"非 BMP: 𠀀 😀 END | α ā — €",
}
rendered={bytes(event["value"]["bytes"]) for event in events if event["event"]=="render"}
for key,value in expected.items():
    registered={bytes(event["bytes"]) for event in events if event["event"]=="register" and event["key"]==key}
    assert value.encode("utf-8") in registered, f"UTF-8 lost during registration: {key}"
    assert value.encode("utf-8") in rendered, f"Text did not reach drawing intact: {key}"
glyphs={event["slot"] for event in events if event["event"]=="draw-glyph" and event["present"]}
assert {0x4e2d,0x6587,0x5355,0x6e38}.issubset(glyphs),"Chinese glyph lookup missing"
assert 0x2026 in glyphs,"Non-BMP missing-glyph placeholder not observed"
wraps=[event for event in events if event["event"]=="draw-newline" and bytes(event["source"]).startswith("这是使用标准".encode("utf-8"))]
assert wraps and not any(event["present"] for event in wraps),"Wrapped text lost newline control semantics"
print("PASS: standard UTF-8 values reached registration and drawing intact; Chinese glyphs and placeholder were observed.")
