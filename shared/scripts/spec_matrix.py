#!/usr/bin/env python3
"""Generate docs/specs/COMPLETENESS-MATRIX.md.

Joins every acceptance ID in the spec files (docs/specs, docs/control-specs/features,
docs/visual-feedback) to the tests that implement it, the implementation entry points and the
UI wiring, and lists every host parameter with the UI files that reference it.

Evidence rules (nothing is trusted without a text search):
  * a test is evidence only if its file exists and, when a needle is given, the needle text occurs
    in that file;
  * an implementation / UI symbol is evidence only if the symbol text occurs in the named file;
  * the literal ID in a test label counts as evidence.
Per-ID evidence beyond what the spec rows themselves say lives in docs/specs/completeness-map.json:
  "<ID>": {"tests": ["path::needle", ...], "status": "MANUAL|DEFERRED", "reason": "..."}
  "_spec:<spec path>": {"impl": ["path::symbol", ...], "ui": ["path::symbol", ...], "ui_note": "..."}

Usage:  python3 shared/scripts/spec_matrix.py            # rewrite the matrix
        python3 shared/scripts/spec_matrix.py --check    # exit 1 if stale or anything is unresolved
Status words: TESTED, FILE-LEVEL (test file named, no assertion located), MANUAL (needs a native
host, device or human), DEFERRED (recorded non-goal), GAP (nothing found, no justification).
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "docs", "specs", "COMPLETENESS-MATRIX.md")
MAP = os.path.join(ROOT, "docs", "specs", "completeness-map.json")

SPEC_GLOBS = [
    "docs/specs/*.md",
    "docs/control-specs/features/*.md",
    "docs/visual-feedback/SPEC.md",
    "docs/visual-feedback/INTEGRATION-PLAN.md",
]
SKIP = {"README.md", "C01-C06-DEVIATIONS.md", "COMPLETENESS-MATRIX.md"}

ID_RE = re.compile(r"\b((?:[A-Z][A-Z0-9]{0,3})(?:-[A-Z][A-Z0-9]{0,3})*-(?:T|[A-Z])?\d{1,2}[a-z]?)\b")
ROW_RE = re.compile(r"^\|\s*([^|]+?)\s*\|")


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as f:
        return f.read()


def load_tree(sub, exts):
    texts = {}
    for ext in exts:
        for p in sorted(glob.glob(os.path.join(ROOT, sub, "**", "*." + ext), recursive=True)):
            rel = os.path.relpath(p, ROOT).replace(os.sep, "/")
            with open(p, encoding="utf-8", errors="replace") as f:
                texts[rel] = f.read()
    return texts


_FILE_CACHE = {}


def any_file(path):
    """Text of a repo file (cached), or None if it does not exist."""
    if path not in _FILE_CACHE:
        full = os.path.join(ROOT, path)
        if os.path.isfile(full):
            with open(full, encoding="utf-8", errors="replace") as f:
                _FILE_CACHE[path] = f.read()
        else:
            _FILE_CACHE[path] = None
    return _FILE_CACHE[path]


def spec_files():
    out = []
    for g in SPEC_GLOBS:
        for p in sorted(glob.glob(os.path.join(ROOT, g))):
            rel = os.path.relpath(p, ROOT).replace(os.sep, "/")
            if os.path.basename(rel) not in SKIP:
                out.append(rel)
    return out


def extract_entries(text):
    """(id, description, table cells) for acceptance IDs: table rows first, then prose IDs."""
    entries, seen = [], set()
    lines = text.split("\n")
    for line in lines:
        m = ROW_RE.match(line) if line.startswith("|") else None
        if not m:
            continue
        cell = m.group(1).replace("`", "")
        if not re.match(r"^[A-Z]", cell):
            continue
        ids = ID_RE.findall(cell)
        if not ids:
            continue
        cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
        for i in ids:
            if i not in seen:
                seen.add(i)
                entries.append((i, cells[1] if len(cells) > 1 else "", cells))
    flat = " ".join(lines)
    prose = list(re.finditer(r"\b((?:C0[1-6]|VF)-T\d\d)\b:?", flat))
    for n, m in enumerate(prose):
        i = m.group(1)
        if i in seen:
            continue
        end = prose[n + 1].start() if n + 1 < len(prose) else min(len(flat), m.end() + 400)
        seen.add(i)
        entries.append((i, flat[m.end():end].strip()[:300], []))
    return entries


def resolve(eid, cells, tests, mp):
    """Return (status, evidence list, note)."""
    ev, note = [], ""
    m = mp.get(eid, {})
    for item in m.get("tests", []):
        path, _, needle = item.partition("::")
        body = tests.get(path) if path in tests else any_file(path)
        if body is not None and (not needle or needle in body):
            ev.append(item if needle else path)
        else:
            note += f" [STALE EVIDENCE: {item}]"
    status = m.get("status")
    note = (m.get("reason", "") + note).strip()
    for p, t in tests.items():
        if re.search(r"\b" + re.escape(eid) + r"\b", t) and not any(e.split("::")[0] == p for e in ev):
            ev.append(p + " (label " + eid + ")")
    stale_row = any(re.match(r"^(MISSING|PARTIAL|CI\b)", c) for c in cells[1:])
    if not ev and cells and not stale_row:
        joined = " | ".join(cells[1:])
        files = [f for f in re.findall(r"tests/[\w/]+\.cpp", joined) if f in tests]
        for name in re.findall(r"\b([A-Z][A-Za-z0-9]+Tests)\b", joined):
            for f in tests:
                if os.path.basename(f) == name + ".cpp" and f not in files:
                    files.append(f)
        hit = False
        for n in re.findall(r'"([^"]{4,80})"', joined):
            for f in (files or list(tests)):
                if n in tests.get(f, ""):
                    ev.append(f + "::" + n)
                    hit = True
                    break
        if not hit:
            ev.extend(f + " (file only)" for f in files)
    if status in ("MANUAL", "DEFERRED"):
        return status, ev, note
    if status in ("PARTIAL", "CHECKED"):
        return (status if ev else "GAP"), ev, note
    if "STALE EVIDENCE" in note:
        return "GAP", ev, note
    if ev and all("(file only)" in e for e in ev):
        return "FILE-LEVEL", ev, note
    if ev:
        return "TESTED", ev, note
    return "GAP", ev, note


def parameter_table(src):
    consts = {}
    for m in re.finditer(r'(\w+Id)\s*=\s*"(\w+)"', src.get("src/PluginProcessor.h", "")):
        consts[m.group(1)] = m.group(2)
    ui_files = [p for p in src if p.startswith("src/ui/") or p.startswith("src/PluginEditor")]
    rows = []
    for c, pid in sorted(consts.items(), key=lambda kv: kv[1]):
        used = [p for p in ui_files if re.search(r"\b" + c + r"\b|\"" + pid + r"\"", src[p])]
        rows.append((pid, c, used))
    return rows


def check_symbols(items, src, label, sf, unresolved):
    out = []
    for item in items:
        path, _, needle = item.partition("::")
        body = src.get(path) if path in src else any_file(path)
        ok = body is not None and (not needle or needle in body)
        out.append(f"`{item}`" + ("" if ok else " (NOT FOUND)"))
        if not ok:
            unresolved.append((sf, label + " " + item))
    return out


def main():
    check = "--check" in sys.argv
    mp = {}
    if os.path.isfile(MAP):
        with open(MAP, encoding="utf-8") as f:
            mp = json.load(f)
    tests = load_tree("tests", ["cpp"])
    src = load_tree("src", ["cpp", "h"])
    totals, unresolved, sections = {}, [], []
    for sf in spec_files():
        text = read(sf)
        rows = []
        for eid, desc, cells in extract_entries(text):
            status, ev, note = resolve(eid, cells, tests, mp)
            totals[status] = totals.get(status, 0) + 1
            if status == "GAP":
                unresolved.append((sf, eid))
            rows.append((eid, desc, status, ev, note))
        bad = sorted({p for p in re.findall(r"src/[\w/]+\.(?:h|cpp)", text) if not os.path.isfile(os.path.join(ROOT, p))})
        sections.append((sf, rows, bad, mp.get("_spec:" + sf, {})))

    juce_in_core = sorted(p for p, t in src.items() if p.startswith("src/core/") and re.search(r'#\s*include\s*[<"]juce', t))
    if juce_in_core:
        unresolved.extend(("portability", "juce include in " + p) for p in juce_in_core)
    out = ["# Spec completeness matrix", "",
           "Generated by `shared/scripts/spec_matrix.py`; do not edit by hand. Edit `docs/specs/completeness-map.json` and rerun the script.", "",
           "## Totals", "", "| Status | Count |", "|---|---|"]
    for k in ("TESTED", "PARTIAL", "CHECKED", "FILE-LEVEL", "MANUAL", "DEFERRED", "GAP"):
        out.append(f"| {k} | {totals.get(k, 0)} |")
    out.append(f"| all acceptance IDs | {sum(totals.values())} |")
    out += ["", f"Portability check (visual-feedback VF-03 and AGENTS.md): files under `src/core/` that include a JUCE header: {len(juce_in_core)}"
            + (" (" + ", ".join(juce_in_core) + ")" if juce_in_core else "") + ".",
            "",
            "TESTED: a test or test label was located by text search. PARTIAL: tests cover part of the row; the note says what is not covered. "
            "CHECKED: verified by inspection or by running a script/command (evidence file named), not by a unit test. FILE-LEVEL: the named test file exists but no individual assertion was located. "
            "MANUAL: needs a native host, device or human review (open under issue #19 or a recorded manual step). DEFERRED: a recorded non-goal. "
            "GAP: nothing found and no justification (must be zero).", "",
            "Tests in `tests/PluginProcessorTests.cpp` and `tests/W3ProcessorTests.cpp` need the JUCE build and were not run on the core-only authoring machine; "
            "tests in `tests/core/` run under `ctest`. TESTED here means the test exists and names the behavior, not that it passed on every platform.", "",
            "## Host parameter to UI wiring", "", "| Parameter ID | Constant | UI files that reference it |", "|---|---|---|"]
    for pid, c, used in parameter_table(src):
        out.append(f"| `{pid}` | `{c}` | " + (", ".join(f"`{u}`" for u in used) if used else "none (host automation / MIDI only)") + " |")
    out += ["", "`activeVariation` is driven by the scene buttons through `requestVariation` (spec A02 of the alpha) and by the host, and is realigned by the processor; it has no attachment by design. The per-operation trims `op<N>Enable`, `op<N>Depth`, `op<N>Slice` (spec A06) are created in a loop and are host automation only by design (no inspector control; recorded in A06).", ""]
    for sf, rows, bad, extra in sections:
        out += [f"## {sf}", ""]
        if rows:
            out += ["| ID | Requirement | Status | Test evidence | Note |", "|---|---|---|---|---|"]
            for eid, desc, status, ev, note in rows:
                d = re.sub(r"\s+", " ", desc).replace("|", "/")[:110]
                e = "<br>".join(f"`{x}`" for x in ev[:3]) or "none"
                out.append(f"| {eid} | {d} | {status} | {e} | {note.replace('|', '/')} |")
            out.append("")
        else:
            out += ["No acceptance IDs in this file.", ""]
        impl = check_symbols(extra.get("impl", []), src, "impl", sf, unresolved)
        ui = check_symbols(extra.get("ui", []), src, "ui", sf, unresolved)
        if impl:
            out += ["Implementation entry points: " + "; ".join(impl), ""]
        if ui:
            out += ["UI wiring: " + "; ".join(ui), ""]
        if extra.get("ui_note"):
            out += ["UI note: " + extra["ui_note"], ""]
        if bad:
            out += ["Stale `src/` paths named by the spec: " + ", ".join(f"`{b}`" for b in bad), ""]
            unresolved.extend((sf, "stale path " + b) for b in bad)
    text = "\n".join(out) + "\n"
    if check:
        cur = open(OUT, encoding="utf-8").read() if os.path.isfile(OUT) else ""
        if cur != text:
            print("COMPLETENESS-MATRIX.md is stale; run spec_matrix.py")
            return 1
        if unresolved:
            print("unresolved:", unresolved)
            return 1
        return 0
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote", OUT, totals)
    for u in unresolved:
        print("UNRESOLVED", u)
    return 0


if __name__ == "__main__":
    sys.exit(main())
