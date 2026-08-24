#!/usr/bin/env python3
"""Compare a normalized Linux trace with the committed Windows v2.0.26 golden."""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

FIELDS = ("schema", "seq", "case", "kind", "vk", "sc", "text", "name")


def load_trace(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line_no, raw in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if not raw.strip():
            continue
        try:
            row = json.loads(raw)
        except json.JSONDecodeError as exc:
            raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
        if not isinstance(row, dict):
            raise ValueError(f"{path}:{line_no}: row must be an object")
        missing = [field for field in FIELDS if field not in row]
        extra = sorted(set(row) - set(FIELDS))
        if missing or extra:
            raise ValueError(
                f"{path}:{line_no}: schema mismatch missing={missing} extra={extra}"
            )
        if row["schema"] != 1 or row["seq"] != len(rows) + 1:
            raise ValueError(
                f"{path}:{line_no}: expected schema=1 seq={len(rows)+1}, "
                f"got schema={row['schema']} seq={row['seq']}"
            )
        for field in ("schema", "seq", "vk", "sc", "text"):
            if type(row[field]) is not int:
                raise ValueError(f"{path}:{line_no}: {field} must be an integer")
        for field in ("case", "kind", "name"):
            if not isinstance(row[field], str):
                raise ValueError(f"{path}:{line_no}: {field} must be a string")
        rows.append(row)
    if not rows:
        raise ValueError(f"{path}: empty trace")
    if rows[-1]["case"] != "meta" or rows[-1]["kind"] != "complete":
        raise ValueError(f"{path}: trace has no terminal meta/complete row")
    return rows


def verify_manifest(golden: Path, manifest_path: Path) -> dict:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1:
        raise ValueError(f"{manifest_path}: unsupported manifest schema")
    if manifest.get("trace") != golden.name:
        raise ValueError(f"{manifest_path}: trace filename does not match {golden.name}")
    digest = hashlib.sha256(golden.read_bytes()).hexdigest().upper()
    if digest != manifest.get("trace_sha256", "").upper():
        raise ValueError(
            f"{manifest_path}: trace hash mismatch expected={manifest.get('trace_sha256')} actual={digest}"
        )
    if manifest.get("runtime") != "AutoHotkey v2.0.26 x64":
        raise ValueError(f"{manifest_path}: golden runtime is not pinned to v2.0.26 x64")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("golden", type=Path)
    parser.add_argument("actual", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()
    manifest_path = args.manifest or args.golden.with_suffix(".manifest.json")
    try:
        manifest = verify_manifest(args.golden, manifest_path)
        golden = load_trace(args.golden)
        actual = load_trace(args.actual)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"DIFFERENTIAL_TRACE_INVALID {exc}", file=sys.stderr)
        return 2

    mismatches: list[dict] = []
    for index in range(max(len(golden), len(actual))):
        expected = golden[index] if index < len(golden) else None
        observed = actual[index] if index < len(actual) else None
        if expected != observed:
            mismatches.append(
                {"seq": index + 1, "expected": expected, "actual": observed}
            )

    summary = {
        "schema": 1,
        "result": "pass" if not mismatches else "fail",
        "golden_runtime": manifest["runtime"],
        "golden_rows": len(golden),
        "actual_rows": len(actual),
        "mismatches": mismatches,
        "compared_fields": list(FIELDS),
        "ignored_fields": ["timestamp", "device_id"],
    }
    if args.summary:
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(
            json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
    if mismatches:
        print(
            f"WINDOWS_DIFFERENTIAL_FAIL rows={len(actual)} mismatches={len(mismatches)}",
            file=sys.stderr,
        )
        for mismatch in mismatches[:8]:
            print(json.dumps(mismatch, ensure_ascii=False), file=sys.stderr)
        return 1
    print(
        f"WINDOWS_DIFFERENTIAL_PASS runtime=2.0.26 rows={len(actual)} fields={len(FIELDS)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
