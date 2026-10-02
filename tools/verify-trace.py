"""Check observed localization bytes and glyph events from the isolated game."""
import json
import re
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
messages=[json.loads(line) for line in (ROOT/"private/import-trace.jsonl").read_text(encoding="utf-8").splitlines()]
assert not any(message.get("type")=="error" for message in messages),"Runtime trace contains script errors"
events=[message["payload"] for message in messages if message.get("type")=="send"]
expected={
    "FE_SINGLE_PLAYER":"单人游戏 UTF8",
    "FE_MULTI_PLAYER":"中文与 English",
    "EU4_UNICODE_PROBE":"中文与 English 123，法兰西 é ü ß。",
    "EU4_UNICODE_SCALAR_PROBE":"非 BMP: 𠀀 😀 END | α ā — €",
    "EU4_UNICODE_FORMAT_PROBE":"§Y中文标题§! §G绿色§! §R红色§! £adm£ £dip£ £mil£ END",
    "EU4_UNICODE_COLLISION_PROBE":"尾字节碰撞：代俣俤俧 END",
}
rendered={bytes(event["value"]["bytes"]) for event in events if event["event"]=="render"}
for key,value in expected.items():
    registered={bytes(event["bytes"]) for event in events if event["event"]=="register" and event["key"]==key}
    assert value.encode("utf-8") in registered, f"UTF-8 lost during registration: {key}"
    assert value.encode("utf-8") in rendered, f"Text did not reach drawing intact: {key}"
glyphs={event["slot"] for event in events if event["event"]=="draw-glyph" and event["present"]}
assert {0x4e2d,0x6587,0x5355,0x6e38}.issubset(glyphs),"Chinese glyph lookup missing"
assert {0x20000,0x1f600}.issubset(glyphs),"Four-byte code points did not reach native glyph lookup"
artifacts=[event for event in events if event["event"]=="artifact"]
if artifacts and artifacts[0].get("font_backend")=="system":
    # Check actual glyph geometry against generated font records. Presence by
    # itself would also pass for a missing-glyph placeholder.
    expected_metrics={0x20000:set(),0x1f600:set(),0xd55c:set()}
    for font in (ROOT/"private/test-mod/gfx/fonts").glob("zh-hans-*.fnt"):
        for line in font.read_text(encoding="utf-8").splitlines():
            if not line.startswith("char "): continue
            record={key:int(value) for key,value in re.findall(r"(\w+)=(-?\d+)",line)}
            scalar=record["id"]
            if scalar in expected_metrics:
                expected_metrics[scalar].add(struct.pack("<7hBB",*(record[name] for name in
                    ["x","y","width","height","xoffset","yoffset","xadvance"]),0,0))
    for scalar,metrics in expected_metrics.items():
        observed=[bytes(event["metrics"]) for event in events if event["event"]=="draw-glyph" and event["slot"]==scalar]
        assert observed and all(value in metrics for value in observed),f"Generated glyph was replaced or truncated: {scalar:#x}"
wraps=[event for event in events if event["event"]=="draw-newline" and bytes(event["source"]).startswith("这是使用标准".encode("utf-8"))]
assert wraps and not any(event["present"] for event in wraps),"Wrapped text lost newline control semantics"
colors={event["code"] for event in events if event["event"]=="format-color"}
assert {ord("Y"),ord("G"),ord("R"),ord("!")}.issubset(colors),"UTF-8 color markers did not reach the native color parser"
icons={event["name"] for event in events if event["event"]=="format-icon"}
assert {"adm","dip","mil"}.issubset(icons),"UTF-8 icon delimiters did not preserve resource names"
assert {ord(char) for char in "代俣俤俧"}.issubset(glyphs),"UTF-8 continuation bytes were confused with format markers"
print("PASS: UTF-8 registration, rendering, glyph lookup, wrapping, colors, icons and continuation-byte collision checks.")
