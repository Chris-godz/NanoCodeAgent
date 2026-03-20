#!/usr/bin/env python3
"""run_verify_report.py — Verify target docs (README + book) and write verify_report.json.

Purpose:
    After doc_restructure_or_update, run verify_paths, verify_links, verify_mermaid,
    and verify_commands on the target docs (README.md and book/src per
    doc_scope_decision). Output structured verify_report.json and optionally
    docgen_validation_report.md.

Usage:
    python3 scripts/docgen/run_verify_report.py [--root <repo-root>] [--scope <path>] [--report-json <path>] [--report-md <path>] [--verify-paths-ignore-repo-prefix <path-prefix>]

Exit codes:
    0 — all blocking checks (paths, links, Mermaid render) passed
    1 — one or more blocking checks failed
"""

import argparse
import json
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CHECKERS = [
    ("verify_paths.py", True),
    ("verify_links.py", True),
    ("verify_diagram_specs.py", True),
    ("verify_mermaid.py", True),
    ("verify_commands.py", False),
]


def load_json(root: str, path: str) -> dict | None:
    full = os.path.join(root, path) if not os.path.isabs(path) else path
    if not os.path.isfile(full):
        return None
    try:
        with open(full, "r", encoding="utf-8") as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return None


def target_docs_from_scope(root: str, scope: dict) -> list[str]:
    """Return repo-relative paths of target docs to verify.

    Prefers the explicit ``approved_targets`` list when present;
    otherwise derives targets from update_readme / update_book /
    book_dirs_or_chapters.  Always includes ``book/src/SUMMARY.md``
    when ``update_book`` is true and any book path is in scope.
    """
    approved = scope.get("approved_targets")
    if isinstance(approved, list) and approved:
        paths = [p for p in approved if os.path.isfile(os.path.join(root, p))]
        return sorted(set(paths))

    paths: list[str] = []
    if scope.get("update_readme"):
        if os.path.isfile(os.path.join(root, "README.md")):
            paths.append("README.md")
    if scope.get("update_book"):
        book_src = os.path.join(root, "book", "src")
        book_dirs = scope.get("book_dirs_or_chapters") or ["book/src"]
        for p in book_dirs:
            p_norm = p.replace("\\", "/")
            full = os.path.join(root, p_norm)
            if os.path.isfile(full) and p_norm not in paths:
                paths.append(p_norm)
        if os.path.isdir(book_src):
            for dirpath, _, filenames in os.walk(book_src):
                for fname in sorted(filenames):
                    if not fname.endswith(".md"):
                        continue
                    fpath = os.path.join(dirpath, fname)
                    rel = os.path.relpath(fpath, root).replace("\\", "/")
                    if rel in paths:
                        continue
                    in_scope = any(
                        rel == p
                        or rel.startswith((p.rstrip("/") + "/"))
                        or p == "book/src"
                        for p in book_dirs
                    )
                    if in_scope:
                        paths.append(rel)
        summary = "book/src/SUMMARY.md"
        if summary not in paths and os.path.isfile(os.path.join(root, summary)):
            paths.append(summary)
    return sorted(paths)


def run_check(
    root: str,
    checker: str,
    doc_path: str,
    verify_paths_ignore_repo_prefixes: list[str] | None = None,
) -> tuple[int, str]:
    full_doc = os.path.join(root, doc_path) if not os.path.isabs(doc_path) else doc_path
    cmd = [sys.executable, os.path.join(SCRIPT_DIR, checker), full_doc, "--root", root]
    if checker == "verify_paths.py":
        for prefix in verify_paths_ignore_repo_prefixes or []:
            cmd.extend(["--ignore-repo-prefix", prefix])
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=root)
    out = (r.stdout or "").strip() + "\n" + (r.stderr or "").strip()
    if checker == "verify_mermaid.py":
        return r.returncode, out
    return r.returncode, out[:500] if len(out) > 500 else out


def extract_artifact_report(output: str) -> str:
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("ARTIFACTS: "):
            return stripped.split("ARTIFACTS: ", 1)[1].strip()
    return ""


CHECKER_TO_ISSUE_TYPE = {
    "verify_paths.py": "invalid_path",
    "verify_links.py": "invalid_link",
    "verify_diagram_specs.py": "missing_diagram_spec",
    "verify_mermaid.py": "invalid_mermaid",
    "verify_commands.py": "invalid_command",
}


def parse_failed_check_to_issues(
    doc_path: str, checker_name: str, is_blocking: bool, output: str
) -> list[dict]:
    """Turn a failed check output into a list of issue dicts for blocking_issues / non_blocking_issues."""
    issue_type = CHECKER_TO_ISSUE_TYPE.get(checker_name, "verify_failed")
    issues: list[dict] = []
    if "FAIL:" in output or "invalid" in output.lower() or "error" in output.lower():
        # Try to extract bullet lines like "  - path" or "  - detail"
        for line in output.splitlines():
            line = line.strip()
            if line.startswith("- ") and len(line) > 2:
                detail = line[2:].strip()
                issues.append(
                    {
                        "type": issue_type,
                        "file": doc_path,
                        "detail": detail,
                        "suggested_fix": "",
                    }
                )
        if not issues:
            issues.append(
                {
                    "type": issue_type,
                    "file": doc_path,
                    "detail": output[:400] if len(output) > 400 else output,
                    "suggested_fix": "",
                }
            )
    return issues


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Verify target docs and write verify_report.json."
    )
    parser.add_argument("--root", default=".", help="Repository root.")
    parser.add_argument(
        "--scope", default=None, help="Path to doc_scope_decision.json."
    )
    parser.add_argument(
        "--report-json", default=None, help="Output verify_report.json path."
    )
    parser.add_argument(
        "--report-md", default=None, help="Optional docgen_validation_report.md path."
    )
    parser.add_argument(
        "--verify-paths-ignore-repo-prefix",
        action="append",
        default=[],
        help="Repo-relative path prefix to skip only for verify_paths.py. Repeat as needed.",
    )
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    scope_path = args.scope or os.path.join(
        root, "docs", "generated", "doc_scope_decision.json"
    )
    report_json = args.report_json or os.path.join(
        root, "docs", "generated", "verify_report.json"
    )
    report_md = args.report_md or os.path.join(
        root, "docs", "generated", "docgen_validation_report.md"
    )

    scope = load_json(root, scope_path)
    if not scope:
        # No scope: verify README + all book/src if they exist
        scope = {
            "update_readme": True,
            "update_book": True,
            "book_dirs_or_chapters": ["book/src"],
        }

    target_files = target_docs_from_scope(root, scope)
    if not target_files:
        target_files = []
        if os.path.isfile(os.path.join(root, "README.md")):
            target_files.append("README.md")
        book_src = os.path.join(root, "book", "src")
        if os.path.isdir(book_src):
            for dirpath, _, filenames in os.walk(book_src):
                for fname in sorted(filenames):
                    if fname.endswith(".md"):
                        target_files.append(
                            os.path.relpath(os.path.join(dirpath, fname), root).replace(
                                "\\", "/"
                            )
                        )

    results: list[dict] = []
    blocking_failed = False
    blocking_issues: list[dict] = []
    non_blocking_issues: list[dict] = []

    for doc_path in target_files:
        full_path = os.path.join(root, doc_path)
        if not os.path.isfile(full_path):
            continue
        row = {"file": doc_path, "checks": []}
        for checker_name, is_blocking in CHECKERS:
            code, out = run_check(
                root,
                checker_name,
                full_path,
                verify_paths_ignore_repo_prefixes=args.verify_paths_ignore_repo_prefix,
            )
            passed = code == 0
            if is_blocking and not passed:
                blocking_failed = True
            row["checks"].append(
                {
                    "name": checker_name,
                    "blocking": is_blocking,
                    "passed": passed,
                    "output": out,
                    "artifact_report": extract_artifact_report(out),
                }
            )
            if not passed:
                parsed = parse_failed_check_to_issues(
                    doc_path, checker_name, is_blocking, out
                )
                if is_blocking:
                    blocking_issues.extend(parsed)
                else:
                    non_blocking_issues.extend(parsed)
        results.append(row)

    approved = scope.get("approved_targets") or []
    not_checked = sorted(set(approved) - set(target_files))
    coverage_note = ""
    if not_checked:
        coverage_note = f"approved_targets not verified (file missing or unsupported): {not_checked}"

    report = {
        "scope": {
            "update_readme": scope.get("update_readme"),
            "update_book": scope.get("update_book"),
        },
        "approved_targets": approved,
        "target_docs_checked": target_files,
        "not_checked": not_checked,
        "verify_coverage_note": coverage_note,
        "per_file": results,
        "blocking_passed": not blocking_failed,
        "blocking_issues": blocking_issues,
        "non_blocking_issues": non_blocking_issues,
    }

    os.makedirs(os.path.dirname(report_json) or ".", exist_ok=True)
    with open(report_json, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"Wrote verify report to {report_json}")

    if report_md:
        lines = ["# Docgen Validation Report (target docs: README + book)\n\n"]
        lines.append("## Summary\n\n")
        lines.append(f"- Target docs checked: {len(results)}")
        lines.append(
            f"- Blocking (paths, links, Mermaid render): {'all passed' if report['blocking_passed'] else 'FAILED'}\n\n"
        )
        lines.append("## Per-file\n\n")
        for row in results:
            lines.append(f"### `{row['file']}`\n")
            for c in row["checks"]:
                status = "Pass" if c["passed"] else "Fail"
                bl = " (blocking)" if c["blocking"] else " (non-blocking)"
                lines.append(f"- **{c['name']}**: {status}{bl}\n")
        os.makedirs(os.path.dirname(report_md) or ".", exist_ok=True)
        with open(report_md, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
        print(f"Wrote validation report to {report_md}")

    if blocking_failed:
        print(
            "=== verify_report blocking failure (summary on stderr) ===",
            file=sys.stderr,
        )
        for row in results:
            for c in row["checks"]:
                if c.get("blocking") and not c.get("passed"):
                    fn = row.get("file", "")
                    chk = c.get("name", "")
                    ar = (c.get("artifact_report") or "").strip()
                    extra = f" artifact_report={ar}" if ar else ""
                    print(f"FAILED: file={fn} checker={chk}{extra}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
