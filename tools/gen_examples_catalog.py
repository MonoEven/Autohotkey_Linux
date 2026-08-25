#!/usr/bin/env python3
"""Generate a user-facing example page for every Linux IMPL worklist entry.

The Linux-verified source is an executable doc-check assertion or a curated
example. Upstream documentation snippets are retained as references, but are
never counted as Linux verification by themselves.
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKLIST = ROOT / "tests/doccheck/worklist.tsv"
DOC_INDEX = ROOT / "tests/doccheck/doc_index.tsv"
PARITY = ROOT / "tests/doccheck/parity.tsv"
GENERATED = ROOT / "examples/generated"
CATALOG = ROOT / "examples/catalog.json"
COVERAGE = ROOT / "examples/FUNCTION_COVERAGE.md"
PROFILES = ROOT / "examples/PROFILE_INDEX.md"

PROFILE_COMMANDS = {
    "headless": 'bash tests/doccheck/run_check.sh "$BIN"',
    "x11": 'bash tests/doccheck/run_check.sh --xvfb "$BIN"',
    "wayland": 'bash tests/doccheck/wayland_run.sh "$BIN"',
    "desktop-session": 'bash tests/oracle/run_gui_host_matrix.sh "$BIN"',
    "dbus": 'bash tests/doccheck/run_check.sh --xvfb "$BIN"',
    "host-tools": 'bash tests/doccheck/run_check.sh --xvfb "$BIN"',
    "interactive": '"$BIN" examples/interactive/input_box.ahk',
    "lifecycle": 'bash examples/run.sh lifecycle "$BIN"',
    "safety-boundary": '"$BIN" examples/safety/shutdown_requires_confirmation.ahk',
}

X11_SUITES = {
    "assert_win.ahk", "assert_display.ahk", "assert_monitor.ahk",
    "assert_timer.ahk", "assert_msg.ahk", "assert_shape.ahk",
    "assert_gui.ahk", "assert_hotkey_btn.ahk", "assert_image.ahk",
    "assert_hotkey_lr.ahk", "assert_unicode_lease.ahk",
    "assert_hotkey_pt.ahk", "assert_clipboard.ahk", "assert_repeat.ahk",
    "assert_clipboard_slow.ahk", "assert_clipboard_change.ahk",
    "assert_input.ahk", "assert_dialog.ahk", "assert_layout.ahk",
    "assert_misc_cov.ahk", "assert_hotstring.ahk", "assert_inputhook.ahk",
    "assert_hotkey.ahk", "assert_edit.ahk", "assert_ctrl.ahk",
}

CURATED = {
    "Array": ("examples/language/runtime_types.ahk", "values := Array"),
    "Class": ("examples/language/runtime_types.ahk", "class ExampleRecord"),
    "Enumerator": ("examples/language/runtime_types.ahk", "enumObject :="),
    "Func": ("examples/language/runtime_types.ahk", "functionObject :="),
    "String": ("examples/language/runtime_types.ahk", "text := String"),
    "GuiControl": ("examples/gui/control_types.ahk", "editCtrl :="),
    "ListView": ("examples/gui/control_types.ahk", 'listCtrl := guiWindow.Add("ListView"'),
    "TreeView": ("examples/gui/control_types.ahk", 'treeCtrl := guiWindow.Add("TreeView"'),
    "InputBox": ("examples/interactive/input_box.ahk", "result := InputBox"),
    "Exit": ("examples/lifecycle/exit_current_thread.ahk", "Exit 7"),
    "Reload": ("examples/lifecycle/reload_once.ahk", "    Reload"),
    "Shutdown": ("examples/safety/shutdown_requires_confirmation.ahk", "Shutdown 0"),
}

CURATED_PROFILE = {
    "Array": "headless", "Class": "headless", "Enumerator": "headless",
    "Func": "headless", "String": "headless",
    "GuiControl": "x11", "ListView": "x11", "TreeView": "x11",
    "InputBox": "interactive", "Exit": "lifecycle", "Reload": "lifecycle",
    "Shutdown": "safety-boundary",
}

CURATED_KIND = {
    "InputBox": "interactive",
    "Exit": "lifecycle",
    "Reload": "lifecycle",
    "Shutdown": "safety-boundary",
}

# AutoHotkey exposes both spellings. They collide on case-insensitive filesystems
# even though the function names are distinct, so the compatibility spelling
# gets an explicit stable suffix.
PAGE_OVERRIDES = {
    "DriveGetFilesystem": "DriveGetFilesystem-compat-spelling.md",
}


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def read_parity() -> dict[str, tuple[str, str]]:
    result: dict[str, tuple[str, str]] = {}
    for raw in PARITY.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        parts = raw.split("\t", 2)
        if len(parts) >= 2:
            result[parts[0]] = (parts[1], parts[2] if len(parts) == 3 else "")
    return result


def defines_local_function(lines: list[str], name: str) -> bool:
    start = re.compile(rf"^\s*{re.escape(name)}\s*\([^()]*\)\s*(.*)$", re.IGNORECASE)
    for index, line in enumerate(lines):
        match = start.match(line)
        if not match:
            continue
        suffix = match.group(1).lstrip()
        if suffix.startswith("{") or suffix.startswith("=>"):
            return True
        if not suffix:
            for following in lines[index + 1:]:
                if not following.strip() or following.lstrip().startswith(";"):
                    continue
                if following.lstrip().startswith("{"):
                    return True
                break
    return False


def call_candidates(name: str) -> list[tuple[Path, int]]:
    pattern = re.compile(rf"(?<![A-Za-z0-9_]){re.escape(name)}\s*\(", re.IGNORECASE)
    found: list[tuple[Path, int]] = []
    for path in sorted((ROOT / "tests/doccheck").glob("assert_*.ahk")):
        lines = path.read_text(encoding="utf-8").splitlines()
        # A local helper shadows the built-in case-insensitively throughout the
        # script. Neither its definition nor calls to it verify the IMPL entry.
        if defines_local_function(lines, name):
            continue
        for line_no, line in enumerate(lines, 1):
            if line.lstrip().startswith(";"):
                continue
            if pattern.search(line):
                found.append((path, line_no))
    found.sort(key=lambda item: (item[0].name == "assert_misc_cov.ahk", item[0].name, item[1]))
    return found


def find_curated(name: str) -> tuple[Path, int]:
    rel, needle = CURATED[name]
    path = ROOT / rel
    lines = path.read_text(encoding="utf-8").splitlines()
    for line_no, line in enumerate(lines, 1):
        if needle in line and not line.lstrip().startswith(";"):
            return path, line_no
    for line_no, line in enumerate(lines, 1):
        if needle in line:
            return path, line_no
    raise ValueError(f"{name}: curated needle {needle!r} not found in {rel}")


def profile_for(path: Path, name: str) -> str:
    if name in CURATED_PROFILE:
        return CURATED_PROFILE[name]
    base = path.name
    if base == "assert_wayland.ahk":
        return "wayland"
    if base == "assert_ime.ahk":
        return "desktop-session"
    if base == "assert_com.ahk":
        return "dbus"
    if base == "assert_sound_etc.ahk":
        return "host-tools"
    if base in X11_SUITES:
        return "x11"
    return "headless"


def kind_for(name: str, parity_level: str, source: Path) -> str:
    if name in CURATED_KIND:
        return CURATED_KIND[name]
    if parity_level == "P4" or source.name == "assert_notimpl.ahk":
        return "verified-error"
    return "verified"


def excerpt(path: Path, line_no: int) -> str:
    lines = path.read_text(encoding="utf-8").splitlines()
    start = max(0, line_no - 3)
    end = min(len(lines), line_no + 3)
    return "\n".join(line.rstrip() for line in lines[start:end]).rstrip()


def page_name(name: str) -> str:
    return PAGE_OVERRIDES.get(name, name + ".md")


def md_escape(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", " ")


def render_page(entry: dict) -> str:
    source = entry["verified_source"]
    source_link = "../../" + source["file"].replace("\\", "/") + f"#L{source['line']}"
    docs = entry.get("documentation")
    lines = [
        f"# {entry['name']}",
        "",
        f"- Linux status: `IMPL` ({entry['parity']['level']})",
        f"- Example kind: `{entry['kind']}`",
        f"- Environment profile: `{entry['profile']}`",
        f"- Verified source: [{source['file']}:{source['line']}]({source_link})",
        f"- Profile command: `{entry['run']}`",
    ]
    if entry["parity"]["note"]:
        lines.append(f"- Linux adaptation: {entry['parity']['note']}")
    if len(entry["verified_sources"]) > 1:
        lines += ["", "## Additional verified environments", ""]
        for extra in entry["verified_sources"][1:]:
            extra_link = "../../" + extra["file"] + f"#L{extra['line']}"
            lines.append(f"- `{extra['profile']}`: [{extra['file']}:{extra['line']}]({extra_link})")
    if entry.get("description"):
        lines += ["", entry["description"]]
    if entry.get("syntax"):
        lines += ["", "## Syntax", "", "````text", entry["syntax"], "````"]
    lines += [
        "",
        "## Linux-verified example excerpt",
        "",
        "This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.",
        "",
        "````ahk",
        source["excerpt"],
        "````",
    ]
    if docs:
        doc_link = "../../" + docs["file"].replace("\\", "/")
        lines += ["", "## Upstream reference example", "", f"Source: [{docs['file']}]({doc_link})"]
        if docs.get("example"):
            lines += ["", "````ahk", docs["example"], "````"]
        else:
            lines += ["", "The upstream page has no standalone code block; use the Linux-verified excerpt above."]
    if entry["kind"] == "safety-boundary":
        lines += [
            "",
            "## Safety boundary",
            "",
            "The default example refuses the destructive operation. It requires an explicit acknowledgement argument and only demonstrates logoff; save all work before opting in.",
        ]
    elif entry["kind"] == "interactive":
        lines += ["", "## Interaction", "", "This example intentionally waits for user input and is excluded from unattended runs."]
    return "\n".join(lines).rstrip() + "\n"


def build() -> tuple[list[dict], dict[Path, str]]:
    impl = [row for row in read_tsv(WORKLIST) if row.get("status") == "IMPL"]
    docs_by_name = {row["name"]: row for row in read_tsv(DOC_INDEX)}
    parity = read_parity()
    entries: list[dict] = []
    outputs: dict[Path, str] = {}
    errors: list[str] = []

    for row in impl:
        name = row["name"]
        try:
            if name in CURATED:
                source_path, line_no = find_curated(name)
                candidates = [(source_path, line_no)]
            else:
                candidates = call_candidates(name)
                if not candidates:
                    raise ValueError("no executable call or curated mapping")
                source_path, line_no = candidates[0]
        except ValueError as exc:
            errors.append(f"{name}: {exc}")
            continue
        rel_source = source_path.relative_to(ROOT).as_posix()
        level, note = parity.get(name, ("P1", ""))
        profile = profile_for(source_path, name)
        verified_sources = []
        seen_files = set()
        for candidate_path, candidate_line in candidates:
            candidate_rel = candidate_path.relative_to(ROOT).as_posix()
            if candidate_rel in seen_files:
                continue
            seen_files.add(candidate_rel)
            verified_sources.append({
                "file": candidate_rel,
                "line": candidate_line,
                "profile": profile_for(candidate_path, name),
            })
        doc = docs_by_name.get(name)
        documentation = None
        if doc:
            doc_path = ROOT / "docs-v2/docs/lib" / f"{name}.htm"
            if not doc_path.is_file():
                errors.append(f"{name}: doc index points to missing page {doc_path.relative_to(ROOT)}")
                continue
            upstream_example = (doc.get("example_code") or "").replace(" | ", "\n")
            upstream_example = "\n".join(
                line.rstrip() for line in upstream_example.splitlines()
            ).rstrip()
            documentation = {
                "file": doc_path.relative_to(ROOT).as_posix(),
                "example": upstream_example,
                "has_code": bool(upstream_example),
            }
        entry = {
            "name": name,
            "page": f"examples/generated/{page_name(name)}",
            "status": "IMPL",
            "kind": kind_for(name, level, source_path),
            "profile": profile,
            "run": PROFILE_COMMANDS[profile],
            "description": row.get("doc_desc") or (doc.get("desc") if doc else "") or "",
            "syntax": row.get("doc_syntax") or (doc.get("syntax") if doc else "") or "",
            "parity": {"level": level, "note": note},
            "verified_source": {
                "file": rel_source,
                "line": line_no,
                "excerpt": excerpt(source_path, line_no),
            },
            "verified_sources": verified_sources,
            "documentation": documentation,
        }
        entries.append(entry)
        outputs[GENERATED / page_name(name)] = render_page(entry)

    if errors:
        raise ValueError("unmapped IMPL entries:\n" + "\n".join(errors))
    names = [entry["name"] for entry in entries]
    if len(names) != len(set(names)) or len(names) != len(impl):
        raise ValueError(f"catalog cardinality mismatch entries={len(names)} impl={len(impl)} unique={len(set(names))}")
    page_names = [page_name(name) for name in names]
    page_names_folded = [name.casefold() for name in page_names]
    if len(page_names_folded) != len(set(page_names_folded)):
        collisions = [name for name, count in Counter(page_names_folded).items() if count > 1]
        raise ValueError(f"case-insensitive generated-page collision: {collisions}")

    profile_counts = Counter(entry["profile"] for entry in entries)
    profile_evidence_counts = Counter()
    for entry in entries:
        for source_profile in {source["profile"] for source in entry["verified_sources"]}:
            profile_evidence_counts[source_profile] += 1
    kind_counts = Counter(entry["kind"] for entry in entries)
    official_count = sum(bool((entry.get("documentation") or {}).get("has_code")) for entry in entries)
    catalog = {
        "schema": 1,
        "source": "tests/doccheck/worklist.tsv",
        "impl_count": len(entries),
        "mapped_count": len(entries),
        "official_reference_examples": official_count,
        "verified_source_links": sum(len(entry["verified_sources"]) for entry in entries),
        "multi_environment_entries": sum(len({source["profile"] for source in entry["verified_sources"]}) > 1 for entry in entries),
        "profiles": dict(sorted(profile_counts.items())),
        "profile_evidence": dict(sorted(profile_evidence_counts.items())),
        "kinds": dict(sorted(kind_counts.items())),
        "entries": entries,
    }
    outputs[CATALOG] = json.dumps(catalog, ensure_ascii=False, indent=2, sort_keys=True) + "\n"

    verified_source_count = sum(len(entry["verified_sources"]) for entry in entries)
    multi_environment_count = sum(
        len({source["profile"] for source in entry["verified_sources"]}) > 1
        for entry in entries
    )
    table = [
        "# Complete Function Example Coverage",
        "",
        f"Generated from `tests/doccheck/worklist.tsv`: **{len(entries)}/{len(entries)} IMPL entries mapped**.",
        f"Linux-verified source links: **{verified_source_count}**; multi-environment entries: **{multi_environment_count}**; upstream reference code blocks: **{official_count}**.",
        "Every row has Linux-executed evidence or an explicit interaction/safety boundary; an upstream snippet alone never counts as coverage.",
        "",
        "| Function | Profile | Kind | Linux-verified example | Upstream code |",
        "|---|---|---|---|---:|",
    ]
    for entry in entries:
        src = entry["verified_source"]
        generated = f"generated/{page_name(entry['name'])}"
        upstream = "yes" if (entry.get("documentation") or {}).get("has_code") else "no"
        table.append(
            f"| [{md_escape(entry['name'])}]({generated}) | `{entry['profile']}` | `{entry['kind']}` | "
            f"[{src['file']}:{src['line']}](../{src['file']}#L{src['line']}) | {upstream} |"
        )
    outputs[COVERAGE] = "\n".join(table) + "\n"

    profile_doc = [
        "# Example Environment Profiles",
        "",
        "Examples are mapped to the least environment in which their Linux behavior is actually verified.",
        "",
        "| Profile | Primary examples | Functions with evidence | Command |",
        "|---|---:|---:|---|",
    ]
    profiles = sorted(set(profile_counts) | set(profile_evidence_counts))
    for profile in profiles:
        profile_doc.append(
            f"| `{profile}` | {profile_counts.get(profile, 0)} | "
            f"{profile_evidence_counts.get(profile, 0)} | `{PROFILE_COMMANDS[profile]}` |"
        )
    profile_doc += [
        "",
        "`safety-boundary` examples refuse destructive behavior by default. `interactive` examples are intentionally not run unattended.",
    ]
    outputs[PROFILES] = "\n".join(profile_doc) + "\n"
    return entries, outputs


def apply(outputs: dict[Path, str], check: bool) -> list[str]:
    differences: list[str] = []
    expected_generated = {path.resolve() for path in outputs if path.parent == GENERATED}
    existing_generated = {path.resolve() for path in GENERATED.glob("*.md")} if GENERATED.exists() else set()
    for path, content in outputs.items():
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current != content:
            differences.append(str(path.relative_to(ROOT)))
            if not check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8", newline="\n")
    stale = sorted(existing_generated - expected_generated)
    for resolved in stale:
        path = Path(resolved)
        differences.append(str(path.relative_to(ROOT)) + " (stale)")
        if not check:
            path.unlink()
    return differences


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if generated outputs differ")
    args = parser.parse_args()
    try:
        entries, outputs = build()
    except (OSError, ValueError, csv.Error) as exc:
        print(f"EXAMPLES_CATALOG_FAIL {exc}", file=sys.stderr)
        return 1
    differences = apply(outputs, args.check)
    official = sum(bool((entry.get("documentation") or {}).get("has_code")) for entry in entries)
    if args.check and differences:
        for path in differences:
            print(f"EXAMPLES_CATALOG_DRIFT {path}", file=sys.stderr)
        return 1
    action = "CHECK_PASS" if args.check else "GENERATED"
    print(
        f"EXAMPLES_CATALOG_{action} impl={len(entries)} mapped={len(entries)} "
        f"official={official} pages={len(entries)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
