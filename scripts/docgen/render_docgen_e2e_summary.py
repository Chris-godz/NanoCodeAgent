#!/usr/bin/env python3
"""render_docgen_e2e_summary.py — Deterministic docgen_e2e_summary.md from pipeline JSON only.

No LLM. All numeric and structural facts must match the cited evidence files.
"""

from __future__ import annotations

import argparse
import json
import os
import sys


def load_json(path: str) -> dict | list | None:
    if not os.path.isfile(path):
        return None
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Render evidence-driven docgen E2E summary.")
    parser.add_argument("--root", default=".", help="Repository root.")
    parser.add_argument(
        "--output",
        default=None,
        help="Output path (default: docs/generated/docgen_e2e_summary.md).",
    )
    args = parser.parse_args()
    root = os.path.abspath(args.root)
    gen = os.path.join(root, "docs", "generated")
    out = args.output or os.path.join(gen, "docgen_e2e_summary.md")

    paths = {
        "change_facts": os.path.join(gen, "change_facts.json"),
        "doc_scope_decision": os.path.join(gen, "doc_scope_decision.json"),
        "verify_report": os.path.join(gen, "verify_report.json"),
        "e2e_loop_state": os.path.join(gen, "e2e_loop_state.json"),
        "e2e_run_evidence": os.path.join(gen, "e2e_run_evidence.json"),
        "verify_repair_report": os.path.join(gen, "verify_repair_report.json"),
        "review_rework_report": os.path.join(gen, "review_rework_report.json"),
        "docgen_review_report_json": os.path.join(gen, "docgen_review_report.json"),
    }

    rel = lambda p: os.path.relpath(p, root).replace("\\", "/")

    lines: list[str] = [
        "# Docgen E2E Summary",
        "",
        "_This run is summarized from JSON evidence only (see **Evidence sources**). No LLM synthesis._",
        "",
        "## 1. Input (change_facts.json)",
        "",
    ]

    cf = load_json(paths["change_facts"])
    if cf is None:
        lines.append("- **Status**: `docs/generated/change_facts.json` **missing**")
    else:
        lines.append("- **Status**: present")
        dr = cf.get("diff_ref")
        lines.append(f"- **diff_ref**: {json.dumps(dr, ensure_ascii=False)}")
        cfiles = cf.get("changed_files") or []
        lines.append(f"- **changed_files** ({len(cfiles)}):")
        for e in cfiles:
            if isinstance(e, dict):
                lines.append(f"  - `{e.get('path', e)}` ({e.get('change_type', '')})")
            else:
                lines.append(f"  - `{e}`")
        pss = cf.get("public_surface_signals")
        if pss is not None:
            lines.append(f"- **public_surface_signals**: {json.dumps(pss, ensure_ascii=False)}")

    lines.extend(["", "## 2. Scope (doc_scope_decision.json)", ""])
    scope = load_json(paths["doc_scope_decision"])
    if scope is None:
        lines.append("- **Status**: **missing**")
    else:
        lines.append("- **Status**: present")
        lines.append(f"- **update_readme**: {scope.get('update_readme')}")
        lines.append(f"- **update_book**: {scope.get('update_book')}")
        lines.append(
            f"- **book_dirs_or_chapters**: {json.dumps(scope.get('book_dirs_or_chapters'), ensure_ascii=False)}"
        )
        lines.append(
            f"- **approved_targets**: {json.dumps(scope.get('approved_targets'), ensure_ascii=False)}"
        )
        r = scope.get("rationale", "")
        lines.append(f"- **rationale** (excerpt): {r[:400]}{'…' if len(r) > 400 else ''}")

    vr = load_json(paths["verify_report"])
    lines.extend(
        [
            "",
            "## 2b. Target typing (README vs book — from scope + verify, no inference)",
            "",
        ]
    )
    if scope is None or vr is None:
        lines.append("- **Status**: **unknown** (scope or verify_report missing)")
    else:
        ur = scope.get("update_readme")
        ub = scope.get("update_book")
        approved = scope.get("approved_targets") or []
        lines.append(f"- **update_readme** (from scope): {ur}")
        lines.append(f"- **update_book** (from scope): {ub}")
        readme_targets = [p for p in approved if isinstance(p, str) and ("README" in p or p == "README.md")]
        if not ur:
            lines.append("- **README targets**: none (update_readme is false)")
        else:
            lines.append(f"- **README targets**: {json.dumps(readme_targets or [], ensure_ascii=False)}")
        book_targets = [p for p in approved if isinstance(p, str) and ("book/" in p or p.startswith("book/"))]
        lines.append(f"- **Book targets** (from approved_targets): {json.dumps(book_targets, ensure_ascii=False)}")
        checked = (vr or {}).get("target_docs_checked") or []
        lines.append(f"- **This run's verified target docs** (verify_report.target_docs_checked): {json.dumps(checked, ensure_ascii=False)}")

    lines.extend(["", "## 3. Validation / verify (verify_report.json)", ""])
    if vr is None:
        lines.append("- **Status**: **missing**")
    else:
        lines.append("- **Status**: present")
        lines.append(f"- **blocking_passed**: {vr.get('blocking_passed')}")
        lines.append(
            f"- **target_docs_checked**: {json.dumps(vr.get('target_docs_checked'), ensure_ascii=False)}"
        )
        issues = vr.get("blocking_issues") or []
        lines.append(f"- **blocking_issues count**: {len(issues)}")
        mermaid_rows = []
        for row in vr.get("per_file") or []:
            if not isinstance(row, dict):
                continue
            for ch in row.get("checks") or []:
                if not isinstance(ch, dict):
                    continue
                if ch.get("name") == "verify_mermaid.py" and ch.get("artifact_report"):
                    mermaid_rows.append(
                        f"  - `{row.get('file')}` → `{ch.get('artifact_report')}`"
                    )
        if mermaid_rows:
            lines.append("- **Mermaid artifact_report paths**:")
            lines.extend(mermaid_rows)

    lines.extend(["", "## 4. Loop state (e2e_loop_state.json)", ""])
    st = load_json(paths["e2e_loop_state"])
    if st is None:
        lines.append("- **Status**: **missing** (typical for single-pass open E2E; retry counts unknown)")
    else:
        lines.append("- **Status**: present")
        lines.append(f"- **phase**: `{st.get('phase')}`")
        lines.append(f"- **verify_repair_count**: {st.get('verify_repair_count')}")
        lines.append(f"- **review_rework_count**: {st.get('review_rework_count')}")
        lines.append(f"- **total_repair_actions**: {st.get('total_repair_actions')}")

    lines.extend(["", "## 5. Verify repair (verify_repair_report.json)", ""])
    vrr = load_json(paths["verify_repair_report"])
    if vrr is None:
        lines.append("- **Status**: **none** (file absent)")
    else:
        fixed = vrr.get("fixed") or []
        nf = vrr.get("not_fixed") or []
        lines.append("- **Status**: present")
        lines.append(f"- **fixed entries**: {len(fixed)}")
        lines.append(f"- **not_fixed entries**: {len(nf)}")

    lines.extend(["", "## 6. Review rework (review_rework_report.json)", ""])
    rrw = load_json(paths["review_rework_report"])
    if rrw is None:
        lines.append("- **Status**: **none** (file absent — no rework step in this run)")
    else:
        am = rrw.get("addressed_must_fix") or []
        lines.append("- **Status**: present")
        lines.append(f"- **addressed_must_fix count**: {len(am)}")
        for i, item in enumerate(am[:10], 1):
            if isinstance(item, dict):
                lines.append(
                    f"  {i}. `{item.get('file', '')}` — {str(item.get('issue', ''))[:120]}"
                )

    lines.extend(["", "## 7. Diagram evidence (e2e_run_evidence.json)", ""])
    ev = load_json(paths["e2e_run_evidence"])
    if ev is None:
        lines.append("- **Status**: **missing**")
    else:
        lines.append("- **Status**: present")
        de = ev.get("diagram_evidence") or {}
        lines.append(
            f"- **diagram_specs**: {json.dumps(de.get('diagram_specs'), ensure_ascii=False)}"
        )
        lines.append(
            f"- **artifact_reports**: {json.dumps(de.get('artifact_reports'), ensure_ascii=False)}"
        )
        lines.append(
            f"- **screenshots**: {json.dumps(de.get('screenshots'), ensure_ascii=False)}"
        )
        rm = ev.get("report_mentions") or {}
        lines.append(
            f"- **review_mentions** (paths cited in review): {json.dumps(rm.get('review_mentions'), ensure_ascii=False)}"
        )
        lines.append(
            f"- **review_rework_mentions**: {json.dumps(rm.get('review_rework_mentions'), ensure_ascii=False)}"
        )

    lines.extend(["", "## 8. Review (docgen_review_report.json)", ""])
    rj = load_json(paths["docgen_review_report_json"])
    if rj is None:
        lines.append("- **Status**: **missing**")
    else:
        must = rj.get("must_fix") or rj.get("must fix") or []
        lines.append(f"- **must_fix count**: {len(must) if isinstance(must, list) else 'unknown'}")
        oa = rj.get("overall_assessment", "")
        if isinstance(oa, str) and oa:
            lines.append(f"- **overall_assessment** (excerpt): {oa[:500]}{'…' if len(oa) > 500 else ''}")

    blocking_ok = bool(vr and vr.get("blocking_passed"))
    must_n = 0
    if isinstance(rj, dict):
        m = rj.get("must_fix") or rj.get("must fix") or []
        if isinstance(m, list):
            must_n = len(m)

    if blocking_ok and must_n == 0:
        rec = "`ready for human review`"
    elif not blocking_ok:
        rec = "`needs revision before review` (blocking verify failed)"
    else:
        rec = "`needs revision before review` (review must_fix non-empty)"

    lines.extend(
        [
            "",
            "## 9. Final recommendation",
            "",
            f"- **Derived from**: verify_report.blocking_passed + docgen_review_report.json must_fix count",
            f"- **{rec}**",
            "",
            "## Evidence sources",
            "",
            "Facts in sections 1–9 were read only from these paths (relative to repo root); no inference.",
            "",
        ]
    )
    for key, p in sorted(paths.items()):
        exists = os.path.isfile(p)
        lines.append(f"- `{rel(p)}` — {'present' if exists else 'absent'}")
    review_report_md = os.path.join(gen, "docgen_review_report.md")
    lines.append(f"- `{rel(review_report_md)}` — {'present' if os.path.isfile(review_report_md) else 'absent'}")
    lines.append("")

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"Wrote {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
