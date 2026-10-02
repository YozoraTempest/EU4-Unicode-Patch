"""Reject missing, incorrect or rewritten native country-search observations."""
import argparse
import json
from pathlib import Path
import re
from search_cases import CASES

ROOT = Path(__file__).resolve().parents[1]


def verify(path):
    messages = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    if any(m.get("type") == "error" for m in messages):
        raise ValueError("Trace contains script errors")
    artifact = next(m for m in messages if m.get("event") == "artifact")
    if artifact["exe_sha256"] != "9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a":
        raise ValueError("Unexpected game executable")
    if not re.fullmatch(r"[a-f0-9]{64}", artifact["dll_sha256"]):
        raise ValueError("Missing plugin fingerprint")
    events = [m["payload"] for m in messages if m.get("type") == "send"]
    if any(e.get("event") in {"query-failed", "native-exception"} for e in events):
        raise ValueError("Trace contains a failed query or native exception")
    checked = []
    for index, (query, expected, exact) in enumerate(CASES):
        applied = [e for e in events if e.get("event") == "query-applied" and e.get("id") == index]
        if len(applied) != 1 or any(applied[0].get(k) != query for k in ("requested", "stored", "displayed")):
            raise ValueError(f"Original query {index} was not preserved")
        filtered = [e for e in events if e.get("event") == "country-filter" and e.get("id") == index and e.get("examined", 0) > 0]
        if not filtered or any(e["query"] != query or e["displayed"] != query for e in filtered):
            raise ValueError(f"No intact native candidate observation for query {index}")
        for event in filtered:
            matches = event["matched"]
            expected_set = [] if expected is None else [expected]
            if (exact and matches != expected_set) or (not exact and expected not in matches):
                raise ValueError(f"Incorrect native result for query {index}")
        checked.append({"id": index, "query": query, "examined": filtered[-1]["examined"],
            "matched": filtered[-1]["matched"], "original_preserved": True})
    return {"artifact": artifact, "passed": True, "cases": checked,
        "scope": "native diplomacy country-name filter; programmatic widget text, not IME or keyboard"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, default=ROOT / "private/search-trace.jsonl")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = verify(args.trace)
    if args.output:
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(report['cases'])} native query results and original UTF-8 values")
