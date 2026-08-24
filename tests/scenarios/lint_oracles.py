#!/usr/bin/env python3
"""Static acceptance-oracle lint (check_detail0824 M6/T5).

Scenarios are intentionally tiny and use a minimal YAML subset. This lint keeps
PASS predicates tied to scenario-owned evidence and prevents the historical
"any StatusNotifierItem text means success" class of false positive.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ALLOWED_EXPECT = {"pass", "unsupported"}


def parse_yaml(path: Path) -> dict[str, str]:
    fields: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"^([A-Za-z_][A-Za-z0-9_]*):\s*(.*)$", raw)
        if not match:
            continue
        value = match.group(2).strip()
        if len(value) >= 2 and value[0] == value[-1] == '"':
            value = value[1:-1]
        fields[match.group(1)] = value
    return fields


def fail(errors: list[str], path: Path, message: str) -> None:
    errors.append(f"{path.relative_to(ROOT)}: {message}")


def lint() -> list[str]:
    errors: list[str] = []
    ids: set[str] = set()
    yamls = sorted(ROOT.glob("*/scenario.yaml"))
    if not yamls:
        return ["no scenario.yaml files found"]

    for yaml in yamls:
        fields = parse_yaml(yaml)
        scenario_id = fields.get("id", "")
        script = fields.get("script", "")
        script_type = fields.get("script_type", "")
        check = fields.get("check", "")
        expect = fields.get("expect", "")
        needs = fields.get("needs", "")

        if not re.fullmatch(r"[a-z0-9_]+", scenario_id):
            fail(errors, yaml, f"invalid or missing id={scenario_id!r}")
        elif scenario_id in ids:
            fail(errors, yaml, f"duplicate id={scenario_id}")
        ids.add(scenario_id)

        if expect not in ALLOWED_EXPECT:
            fail(errors, yaml, f"expect must be one of {sorted(ALLOWED_EXPECT)}, got {expect!r}")
        if expect == "pass" and (not script or not check):
            fail(errors, yaml, "expected-pass scenario requires both script and check")
        if expect == "unsupported" and not needs:
            fail(errors, yaml, "unsupported scenario must name the missing capability")

        if script:
            script_path = yaml.parent / script
            if not script_path.is_file():
                fail(errors, yaml, f"script does not exist: {script}")
                continue
            source = script_path.read_text(encoding="utf-8", errors="replace")
        else:
            script_path = yaml
            source = ""

        if check.startswith("marker:"):
            marker = check.removeprefix("marker:")
            if not re.fullmatch(r"/tmp/scn_[A-Za-z0-9_]+", marker):
                fail(errors, yaml, f"marker must be an exact scenario-owned /tmp/scn_* path: {marker!r}")
            if script_type == "sh" and marker not in source:
                fail(errors, script_path, f"shell producer never references marker {marker}")
        elif check.startswith("script:"):
            command = check.removeprefix("script:")
            if "/tmp/scn_" not in command:
                fail(errors, yaml, "script check must consume scenario-owned /tmp/scn_* evidence")
            for grep in re.finditer(r"grep\s+[^\n;&|]*?(['\"])(.*?)\1", command):
                pattern = grep.group(2)
                if "^" not in pattern:
                    fail(errors, yaml, f"grep PASS pattern must be anchored: {pattern!r}")
        elif check:
            fail(errors, yaml, "check must use marker: or script: (no opaque shell predicate)")

        # D-Bus monitors observe the whole user session. A generic interface
        # word can match another process; require the scenario's exact dynamic
        # destination and object path before it may create a PASS marker.
        if "dbus-monitor" in source:
            if 'gdbus call --session --dest "$BUS"' not in source:
                fail(errors, script_path, "D-Bus oracle must query its exact $BUS destination")
            if 'destination=$BUS' not in source:
                fail(errors, script_path, "D-Bus monitor PASS must bind destination=$BUS")
            if "path=/StatusNotifierItem;" not in source:
                fail(errors, script_path, "D-Bus monitor PASS must bind the exact object path")

        # Explicitly reject the historical weak predicate even if reintroduced
        # in a different scenario.
        if re.search(r"grep[^\n]*['\"]StatusNotifierItem['\"]", source):
            fail(errors, script_path, "bare StatusNotifierItem grep is not an identity oracle")

    return errors


def main() -> int:
    errors = lint()
    if errors:
        for error in errors:
            print(f"SCENARIO_ORACLE_LINT_FAIL {error}", file=sys.stderr)
        return 1
    count = len(list(ROOT.glob("*/scenario.yaml")))
    print(f"SCENARIO_ORACLE_LINT_PASS scenarios={count} identity_bound_dbus=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
