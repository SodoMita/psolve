#!/usr/bin/env python3
"""tolsheet_check.py - grep-provable closure between the literal tolerances
in src/ and their documentation entries in docs/DESIGN.md section 8
(roadmap 6.8: per-module tolerance semantics sheets).

Closure rules (both directions enforced):

  1. Every CODE line in src/*.{c,h,inc} that contains a tolerance-class
     literal - a decimal exponent float ([0-9]e+-N), a hex-float 0x1pN, or
     DBL_EPSILON - must carry a /* TOLSHEET <ID> */ tag on that same line.
     Lines whose first non-space characters are a comment opener (*, //,
     /*) are prose, not code, and are exempt (they may discuss literals;
     they cannot execute them).
  2. Every TOLSHEET <ID> appearing in src/ must appear as a `TOL-...`
     entry in docs/DESIGN.md (the doc row IS the semantics sheet for the
     site: direction of safety, what it protects, what it may never
     justify).
  3. Every TOL-... ID documented in DESIGN.md section 8 must be carried by
     at least one source line (no stale rows after refactors).

The tolerated-tag set is therefore exactly the documented set; adding a new
tolerance literal to the engine without a sheet entry fails this gate, and
deleting/renaming a documented site fails it too.

Usage: python3 tools/tolsheet_check.py        (exit 1 with a diff on drift)
"""

import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC_DIR = os.path.join(ROOT, "src")
DESIGN = os.path.join(ROOT, "docs", "DESIGN.md")

SRC_EXTS = (".c", ".h", ".inc")

# canonical tolerance-literal pattern (see rule 1)
LITERAL = re.compile(r"(?:[0-9](?:\.[0-9]+)?[eE][+-]?[0-9]+|0x1p[0-9]+|DBL_EPSILON)")
TAG = re.compile(r"TOLSHEET\s+(TOL-[A-Z0-9-]+)")
DOCID = re.compile(r"\b(TOL-[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+)\b")
COMMENT_LINE = re.compile(r"^\s*(?:\*|//|/\*)")


def code_only(text):
    """Return `text` with C comments and string/char literals blanked out
    (spaces), preserving line structure, so tolerance-literal matching sees
    only executable tokens.  A literal inside a comment is prose; a literal
    inside a printf format is display text; neither can steer a verdict."""
    out = []
    i, n = 0, len(text)
    state = "code"   # code | block | line | str | chr
    while i < n:
        c = text[i]
        if state == "code":
            if text.startswith("/*", i):
                state = "block"; out.append("  "); i += 2
            elif text.startswith("//", i):
                state = "line"; out.append("  "); i += 2
            elif c == '"':
                state = "str"; out.append(" "); i += 1
            elif c == "'":
                state = "chr"; out.append(" "); i += 1
            else:
                out.append(c); i += 1
        elif state == "block":
            if text.startswith("*/", i):
                state = "code"; out.append("  "); i += 2
            else:
                out.append("\n" if c == "\n" else " "); i += 1
        elif state == "line":
            if c == "\n":
                state = "code"; out.append("\n")
            else:
                out.append(" ")
            i += 1
        else:  # str / chr
            if c == "\\":
                out.append("  " if text[i:i+2] != "\\\n" else " \n"); i += 2
            elif (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state = "code"; out.append(" "); i += 1
            else:
                out.append("\n" if c == "\n" else " "); i += 1
    return "".join(out)


def main():
    problems = []

    # --- collect source hits & tags -------------------------------------
    src_tags = {}          # id -> [file:line, ...]
    untagged_hits = []
    for dirpath, _dirs, files in os.walk(SRC_DIR):
        for fn in sorted(files):
            if not fn.endswith(SRC_EXTS):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, ROOT)
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                whole = f.read()
            code = code_only(whole)
            raw_lines = whole.splitlines()
            code_lines = code.splitlines()
            for ln, (raw, cl) in enumerate(zip(raw_lines, code_lines), 1):
                for m in TAG.finditer(raw):
                    src_tags.setdefault(m.group(1), []).append(f"{rel}:{ln}")
                if LITERAL.search(cl) and not TAG.search(raw):
                    untagged_hits.append(f"{rel}:{ln}: {cl.strip()[:100]}")

    # --- collect documented IDs ------------------------------------------
    with open(DESIGN, "r", encoding="utf-8") as f:
        design = f.read()
    # scope to section 8 onward (the sheets); IDs elsewhere are prose
    sec8 = design[design.index("\n## 8."):]
    doc_ids = set(DOCID.findall(sec8))

    # --- rule 1 -----------------------------------------------------------
    if untagged_hits:
        problems.append("untagged tolerance literals in src/ (add a "
                        "/* TOLSHEET <ID> */ comment and a DESIGN.md row):")
        problems.extend("  " + h for h in untagged_hits)

    # --- rule 2 -----------------------------------------------------------
    for tid in sorted(set(src_tags) - doc_ids):
        problems.append(f"src/ tag {tid} at {src_tags[tid][0]} has no "
                        f"docs/DESIGN.md section-8 entry")

    # --- rule 3 -----------------------------------------------------------
    for did in sorted(doc_ids - set(src_tags)):
        problems.append(f"docs/DESIGN.md documents {did} but no src/ line "
                        f"carries its TOLSHEET tag (stale row)")

    if problems:
        print("tolsheet_check: DRIFT")
        print("\n".join(problems))
        return 1
    print(f"tolsheet_check: OK ({len(src_tags)} ids, "
          f"{sum(len(v) for v in src_tags.values())} tagged sites, "
          f"{len(doc_ids)} documented rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
