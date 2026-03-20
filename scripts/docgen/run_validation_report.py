#!/usr/bin/env python3
"""run_validation_report.py — Run verify_* on candidates and impact report; write aggregated report.

Purpose:
    For each candidate and change_impact_report.md, run verify_paths, verify_links,
    verify_commands, verify_doc_consistency. Write docgen_validation_report.md.
    Blocking: paths, links. Non-blocking: commands, consistency.

Usage:
    python3 scripts/docgen/run_validation_report.py [--root <repo-root>] [--candidates-dir <path>] [--report <path>]

Exit codes:
    0 — all blocking checks passed
    1 — one or more blocking checks failed
"""

import argparse
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CHECKERS = [
    ("verify_paths.py", True),
    ("verify_links.py", True),
    ("verify_commands.py", False),
    ("verify_doc_consistency.py", False),
]


def run_check(root: str, checker: str, doc_path: str) -> tuple[int, str]:
    """Run checker on doc_path; return (exit_code, stdout+stderr snippet)."""
    cmd = [sys.executable, os.path.join(SCRIPT_DIR, checker), doc_path, "--root", root]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=root)
    out = (r.stdout or "").strip() + "\n" + (r.stderr or "").strip()
    return r.returncode, out[:500] if len(out) > 500 else out


def main() -> None:
    parser = argparse.ArgumentParser(description="Aggregate validation and write report.")
    parser.add_argument("--root", default=".", help="Repository root.")
    parser.add_argument("--candidates-dir", default=None, help="Candidates directory.")
    parser.add_argument("--report", default=None, help="Output report path.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    candidates_dir = args.candidates_dir or os.path.join(root, "docs", "generated", "candidates")
    report_path = args.report or os.path.join(root, "docs", "generated", "docgen_validation_report.md")

    files_to_check: list[str] = []
    if os.path.isdir(candidates_dir):
        for f in sorted(os.listdir(candidates_dir)):
            if f.endswith(".md"):
                files_to_check.append(os.path.join(candidates_dir, f))
    impact_report = os.path.join(root, "docs", "generated", "change_impact_report.md")
    if os.path.isfile(impact_report):
        files_to_check.append(impact_report)

    if not files_to_check:
        os.makedirs(os.path.dirname(report_path) or ".", exist_ok=True)
        with open(report_path, "w", encoding="utf-8") as f:
            f.write("# Docgen Validation Report\n\nNo files to check (no candidates or impact report).\n")
        print("Wrote validation report (no files checked).")
        return

    results: list[dict] = []
    blocking_failed = False

    for doc_path in files_to_check:
        rel = os.path.relpath(doc_path, root).replace("\\", "/")
        row = {"file": rel, "checks": []}
        for checker_name, is_blocking in CHECKERS:
            code, out = run_check(root, checker_name, doc_path)
            passed = code == 0
            if is_blocking and not passed:
                blocking_failed = True
            row["checks"].append({
                "name": checker_name,
                "blocking": is_blocking,
                "passed": passed,
                "output": out,
            })
        results.append(row)

    lines = ["# Docgen Validation Report\n"]
    lines.append("## Summary\n")
    total_files = len(results)
    total_checks = sum(len(r["checks"]) for r in results)
    passed = sum(1 for r in results for c in r["checks"] if c["passed"])
    failed = total_checks - passed
    blocking_ok = not blocking_failed
    lines.append(f"- Files checked: {total_files}")
    lines.append(f"- Checks: {passed} passed, {failed} failed")
    lines.append(f"- **Blocking (paths, links)**: " + ("all passed" if blocking_ok else "**FAILED**"))
    lines.append(f"- Non-blocking (commands, consistency): reported below.\n")
    lines.append("## Per-file results\n")

    for row in results:
        lines.append(f"### `{row['file']}`\n")
        for c in row["checks"]:
            status = "Pass" if c["passed"] else "Fail"
            bl = " (blocking)" if c["blocking"] else " (non-blocking)"
            lines.append(f"- **{c['name']}**: {status}{bl}")
            if not c["passed"] and c["output"]:
                lines.append(f"  ```\n  {c['output'][:300].replace(chr(10), chr(10) + '  ')}\n  ```")
        lines.append("")

    lines.append("## Blocking vs non-blocking\n")
    lines.append("- **Blocking**: verify_paths.py, verify_links.py. Must pass for pipeline to succeed.")
    lines.append("- **Non-blocking**: verify_commands.py, verify_doc_consistency.py. Failures are recorded but do not fail the pipeline.\n")
    lines.append("**Recommendation**: " + ("Proceed to human review." if blocking_ok else "Fix blocking failures before human review.") + "\n")

    os.makedirs(os.path.dirname(report_path) or ".", exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"Wrote {report_path}")

    if blocking_failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
