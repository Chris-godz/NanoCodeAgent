#!/usr/bin/env python3
"""run_docgen_e2e_loop.py — Closed-loop orchestrator: verify → repair (up to 2) → review → rework (up to 2), then deterministic summary.

Assumes Phase 1 (change_facts → scope → reference_context → draft write) has already run.
Reads docs/generated/doc_scope_decision.json, runs verify; on blocking failure runs verify_repair
(up to 2 times, total repair actions ≤ 4); then review; on must_fix runs review_rework (up to 2 times);
then summary or TerminalFail. State persisted to docs/generated/e2e_loop_state.json.

Usage:
    python3 scripts/docgen/run_docgen_e2e_loop.py [--root <repo-root>]

Exit codes:
    0 — summary written, ready for human review
    1 — TerminalFail (verify/review limits or pipeline error)
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))
GENERATED = "docs/generated"
MAX_VERIFY_REPAIR = 2
MAX_REVIEW_REWORK = 2
MAX_TOTAL_REPAIR_ACTIONS = 4

STATE_FILE = "e2e_loop_state.json"
VERIFY_REPORT = "verify_report.json"
VALIDATION_REPORT = "docgen_validation_report.md"
REVIEW_REPORT_MD = "docgen_review_report.md"
REVIEW_REPORT_JSON = "docgen_review_report.json"
VERIFY_REPAIR_REPORT = "verify_repair_report.json"
REVIEW_REWORK_REPORT = "review_rework_report.json"
FAILURE_REPORT = "e2e_failure_report.md"
SUMMARY_REPORT = "docgen_e2e_summary.md"
RUN_EVIDENCE_REPORT = "e2e_run_evidence.json"


def log(msg: str) -> None:
    print(f">>> [{__import__('datetime').datetime.now().strftime('%H:%M:%S')}] {msg}", flush=True)


def load_json(root: str, rel_path: str) -> dict | None:
    full = os.path.join(root, rel_path)
    if not os.path.isfile(full):
        return None
    try:
        with open(full, "r", encoding="utf-8") as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return None


def save_json(root: str, rel_path: str, data: dict) -> None:
    full = os.path.join(root, rel_path)
    os.makedirs(os.path.dirname(full) or ".", exist_ok=True)
    with open(full, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")


def existing_rel_path(root: str, rel_path: str) -> str:
    full = os.path.join(root, rel_path)
    return rel_path if os.path.isfile(full) else ""


def list_relative_files(root: str, rel_dir: str, suffixes: tuple[str, ...]) -> list[str]:
    full_dir = os.path.join(root, rel_dir)
    if not os.path.isdir(full_dir):
        return []
    results: list[str] = []
    for dirpath, _, filenames in os.walk(full_dir):
        for fname in sorted(filenames):
            if suffixes and not fname.endswith(suffixes):
                continue
            rel = os.path.relpath(os.path.join(dirpath, fname), root).replace("\\", "/")
            results.append(rel)
    return sorted(results)


def collect_path_mentions(root: str, report_paths: list[str], candidate_paths: list[str]) -> list[str]:
    mentions: list[str] = []
    for report_path in report_paths:
        full = os.path.join(root, report_path)
        if not os.path.isfile(full):
            continue
        try:
            with open(full, "r", encoding="utf-8") as f:
                content = f.read()
        except OSError:
            continue
        for candidate in candidate_paths:
            if candidate and candidate not in mentions and candidate in content:
                mentions.append(candidate)
    return mentions


def collect_run_evidence(root: str, generated_dir: str, state: dict) -> dict:
    generated_prefix = generated_dir.replace("\\", "/")
    verify_path = os.path.join(generated_dir, VERIFY_REPORT)
    review_md_path = os.path.join(generated_dir, REVIEW_REPORT_MD)
    review_json_path = os.path.join(generated_dir, REVIEW_REPORT_JSON)
    review_rework_path = os.path.join(generated_dir, REVIEW_REWORK_REPORT)
    verify_report = load_json(root, verify_path) or {}

    diagram_specs = list_relative_files(root, os.path.join(generated_dir, "diagram_specs"), (".md",))
    artifact_reports = list_relative_files(root, os.path.join(generated_dir, "diagram_artifacts"), ("report.json",))
    screenshots: list[str] = []
    svgs: list[str] = []
    for artifact_report in artifact_reports:
        data = load_json(root, artifact_report) or {}
        for block in data.get("blocks", []):
            if not isinstance(block, dict):
                continue
            screenshot_path = block.get("screenshot_path") or ""
            svg_path = block.get("svg_path") or ""
            if screenshot_path and screenshot_path not in screenshots:
                screenshots.append(screenshot_path)
            if svg_path and svg_path not in svgs:
                svgs.append(svg_path)

    referenced_artifact_reports: list[str] = []
    for row in verify_report.get("per_file", []):
        if not isinstance(row, dict):
            continue
        for check in row.get("checks", []):
            if not isinstance(check, dict):
                continue
            artifact_report = check.get("artifact_report") or ""
            if artifact_report and artifact_report not in referenced_artifact_reports:
                referenced_artifact_reports.append(artifact_report)

    review_paths = [
        path
        for path in (review_md_path, review_json_path)
        if existing_rel_path(root, path)
    ]
    rework_paths = [
        path
        for path in (review_rework_path,)
        if existing_rel_path(root, path)
    ]
    candidate_paths = diagram_specs + artifact_reports + screenshots

    return {
        "phase": state.get("phase", ""),
        "retry_counts": {
            "verify_repair_count": state.get("verify_repair_count", 0),
            "review_rework_count": state.get("review_rework_count", 0),
            "total_repair_actions": state.get("total_repair_actions", 0),
        },
        "core_reports": {
            "change_facts": existing_rel_path(root, os.path.join(generated_dir, "change_facts.json")),
            "doc_scope_decision": existing_rel_path(root, os.path.join(generated_dir, "doc_scope_decision.json")),
            "reference_context": existing_rel_path(root, os.path.join(generated_dir, "reference_context.json")),
            "verify_report": existing_rel_path(root, verify_path),
            "validation_report": existing_rel_path(root, os.path.join(generated_dir, VALIDATION_REPORT)),
            "review_report_md": existing_rel_path(root, review_md_path),
            "review_report_json": existing_rel_path(root, review_json_path),
            "verify_repair_report": existing_rel_path(root, os.path.join(generated_dir, VERIFY_REPAIR_REPORT)),
            "review_rework_report": existing_rel_path(root, review_rework_path),
            "state_report": existing_rel_path(root, os.path.join(generated_dir, STATE_FILE)),
            "summary_report": existing_rel_path(root, os.path.join(generated_dir, SUMMARY_REPORT)),
        },
        "diagram_evidence": {
            "diagram_specs": diagram_specs,
            "artifact_reports": artifact_reports,
            "screenshots": screenshots,
            "svgs": svgs,
            "artifact_reports_referenced_by_verify": referenced_artifact_reports,
        },
        "report_mentions": {
            "review_paths": review_paths,
            "review_rework_paths": rework_paths,
            "review_mentions": collect_path_mentions(root, review_paths, candidate_paths),
            "review_rework_mentions": collect_path_mentions(root, rework_paths, candidate_paths),
        },
        "generated_prefix": generated_prefix,
    }


def run_verify(root: str, scope_path: str, generated_dir: str) -> tuple[bool, dict | None]:
    """Run run_verify_report.py. Return (blocking_passed, report_dict)."""
    report_path = os.path.join(generated_dir, VERIFY_REPORT)
    cmd = [
        sys.executable,
        os.path.join(SCRIPT_DIR, "run_verify_report.py"),
        "--root", root,
        "--scope", os.path.join(root, scope_path),
        "--report-json", os.path.join(root, report_path),
    ]
    r = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
    report = load_json(root, report_path)
    return bool(report and report.get("blocking_passed")), report


def run_render_e2e_summary(root: str) -> bool:
    script = os.path.join(SCRIPT_DIR, "render_docgen_e2e_summary.py")
    r = subprocess.run(
        [sys.executable, script, "--root", root],
        cwd=root,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if r.returncode != 0:
        log(
            f"render_docgen_e2e_summary.py failed: {(r.stderr or r.stdout or '').strip()[:500]}"
        )
        return False
    return True


def run_codex_task(root: str, task_rel_path: str) -> bool:
    """Run codex exec --full-auto with task file. Return True if exit code 0."""
    full = os.path.join(root, task_rel_path)
    if not os.path.isfile(full):
        return False
    with open(full, "r", encoding="utf-8") as f:
        content = f.read()
    # Long timeout: review/rework steps can take well over 10 minutes; run script in background if needed
    r = subprocess.run(
        ["codex", "exec", "--full-auto", content],
        cwd=root,
        capture_output=True,
        text=True,
        timeout=3600,
    )
    if r.returncode != 0:
        log(f"codex task {task_rel_path} exited {r.returncode}; stderr: {r.stderr[:500] if r.stderr else 'none'}")
    return r.returncode == 0


def classify_review_issues(root: str, generated_dir: str) -> dict:
    """Classify review must-fix items by issue_class.

    Returns {"doc_editable": [...], "pipeline_editable": [...],
             "ambiguous": [...], "stale_or_resolved": [...], "unclassified": [...]}.
    Also sets "has_any_must_fix" and "has_doc_editable_must_fix".
    """
    result: dict = {
        "doc_editable": [],
        "pipeline_editable": [],
        "ambiguous": [],
        "stale_or_resolved": [],
        "unclassified": [],
        "has_any_must_fix": False,
        "has_doc_editable_must_fix": False,
    }
    json_path = os.path.join(generated_dir, REVIEW_REPORT_JSON)
    data = load_json(root, json_path)
    if data is not None:
        must = data.get("must_fix") or data.get("must fix") or []
        if isinstance(must, list):
            result["has_any_must_fix"] = len(must) > 0
            for item in must:
                cls = (item.get("issue_class") or "unclassified") if isinstance(item, dict) else "unclassified"
                bucket = cls if cls in result else "unclassified"
                result[bucket].append(item)
            result["has_doc_editable_must_fix"] = len(result["doc_editable"]) > 0
            return result
    md_path = os.path.join(generated_dir, REVIEW_REPORT_MD)
    full_md = os.path.join(root, md_path)
    if not os.path.isfile(full_md):
        return result
    with open(full_md, "r", encoding="utf-8") as f:
        text = f.read()
    has_must = False
    if re.search(r"(?m)^#+\s*Must\s+fix\s*$", text, re.IGNORECASE):
        has_must = True
    elif ("Must fix" in text or "must fix" in text) and re.search(r"(?m)^\s*[-*]\s+.+", text):
        has_must = True
    result["has_any_must_fix"] = has_must
    if has_must:
        result["has_doc_editable_must_fix"] = True
        result["unclassified"].append({"issue": "must-fix detected from markdown (no JSON classification)", "issue_class": "unclassified"})
    return result


def has_must_fix_from_review(root: str, generated_dir: str) -> bool:
    """True if review report has must-fix items (from .json or heuristic from .md)."""
    return classify_review_issues(root, generated_dir)["has_any_must_fix"]


def write_failure_report(
    root: str,
    generated_dir: str,
    failure_type: str,
    last_verify: str,
    last_review: str,
    *,
    unresolved_reason: str | None = None,
    suggested_next_action: str | None = None,
) -> None:
    path = os.path.join(root, generated_dir, FAILURE_REPORT)
    evidence_rel = os.path.join(generated_dir, RUN_EVIDENCE_REPORT)
    evidence = load_json(root, evidence_rel) or {}
    diagram_evidence = evidence.get("diagram_evidence") if isinstance(evidence, dict) else {}
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("# Docgen E2E Closed Loop — Failure Report\n\n")
        f.write(f"- **Failure type**: {failure_type}\n")
        f.write(f"- **Last verify report**: `{last_verify}`\n")
        f.write(f"- **Last review report**: `{last_review}`\n\n")
        if existing_rel_path(root, evidence_rel):
            f.write(f"- **Run evidence report**: `{evidence_rel}`\n")
            if isinstance(diagram_evidence, dict):
                f.write(f"- **Diagram specs captured**: {len(diagram_evidence.get('diagram_specs', []))}\n")
                f.write(f"- **Diagram artifact reports captured**: {len(diagram_evidence.get('artifact_reports', []))}\n")
                f.write(f"- **Diagram screenshots captured**: {len(diagram_evidence.get('screenshots', []))}\n\n")
        if unresolved_reason:
            f.write(f"- **Unresolved reason**: {unresolved_reason}\n\n")
        if suggested_next_action:
            f.write(f"- **Suggested next action**: {suggested_next_action}\n\n")
        f.write("Check the above reports for details and suggested fixes.\n")
    log(f"Wrote {path}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Docgen E2E closed loop: verify → repair → review → rework → summary")
    ap.add_argument("--root", default=REPO_ROOT, help="Repository root")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    generated_dir = os.path.join("docs", "generated")
    scope_path = os.path.join(generated_dir, "doc_scope_decision.json")
    state_path = os.path.join(generated_dir, STATE_FILE)

    def task_path(name: str) -> str:
        return os.path.relpath(os.path.join(SCRIPT_DIR, "tasks", name), root)

    scope = load_json(root, scope_path)
    if not scope:
        log("Missing doc_scope_decision.json — PipelineTerminalFail")
        write_failure_report(
            root, generated_dir,
            "PipelineTerminalFail",
            os.path.join(generated_dir, VERIFY_REPORT),
            os.path.join(generated_dir, REVIEW_REPORT_MD),
        )
        save_json(
            root,
            os.path.join(generated_dir, RUN_EVIDENCE_REPORT),
            collect_run_evidence(
                root,
                generated_dir,
                {
                    "phase": "TerminalFail",
                    "verify_repair_count": 0,
                    "review_rework_count": 0,
                    "total_repair_actions": 0,
                    "last_verify_report": os.path.join(generated_dir, VERIFY_REPORT),
                    "last_review_report": os.path.join(generated_dir, REVIEW_REPORT_MD),
                    "failure_type": "PipelineTerminalFail",
                    "run_evidence_report": os.path.join(generated_dir, RUN_EVIDENCE_REPORT),
                },
            ),
        )
        return 1

    verify_repair_count = 0
    review_rework_count = 0
    total_repair_actions = 0
    phase = "DraftReady"
    last_verify_report = os.path.join(generated_dir, VERIFY_REPORT)
    last_review_report = os.path.join(generated_dir, REVIEW_REPORT_MD)

    while True:
        state = {
            "phase": phase,
            "verify_repair_count": verify_repair_count,
            "review_rework_count": review_rework_count,
            "total_repair_actions": total_repair_actions,
            "last_verify_report": last_verify_report,
            "last_review_report": last_review_report,
            "run_evidence_report": os.path.join(generated_dir, RUN_EVIDENCE_REPORT),
        }
        save_json(root, os.path.join(generated_dir, RUN_EVIDENCE_REPORT), collect_run_evidence(root, generated_dir, state))
        save_json(root, state_path, state)

        if phase == "DraftReady" or phase == "VerifyRepairing" or phase == "ReviewReworking":
            log("Running verify")
            blocking_passed, report = run_verify(root, scope_path, generated_dir)
            last_verify_report = os.path.join(generated_dir, VERIFY_REPORT)
            if not blocking_passed:
                phase = "VerifyFailed"
                continue
            phase = "VerifyPassed"
            continue

        if phase == "VerifyFailed":
            if verify_repair_count >= MAX_VERIFY_REPAIR or total_repair_actions >= MAX_TOTAL_REPAIR_ACTIONS:
                log("VerifyTerminalFail: repair limit reached")
                write_failure_report(root, generated_dir, "VerifyTerminalFail", last_verify_report, last_review_report)
                terminal_state = {**state, "phase": "TerminalFail", "failure_type": "VerifyTerminalFail"}
                save_json(root, os.path.join(generated_dir, RUN_EVIDENCE_REPORT), collect_run_evidence(root, generated_dir, terminal_state))
                save_json(root, state_path, terminal_state)
                return 1
            log("Running verify_repair")
            if not run_codex_task(root, task_path("ai_verify_repair.md")):
                log("verify_repair task failed — treating as TerminalFail")
                write_failure_report(root, generated_dir, "VerifyTerminalFail", last_verify_report, last_review_report)
                save_json(
                    root,
                    os.path.join(generated_dir, RUN_EVIDENCE_REPORT),
                    collect_run_evidence(root, generated_dir, {**state, "phase": "TerminalFail", "failure_type": "VerifyTerminalFail"}),
                )
                return 1
            verify_repair_count += 1
            total_repair_actions += 1
            phase = "VerifyRepairing"
            continue

        if phase == "VerifyPassed":
            log("Running review")
            if not run_codex_task(root, task_path("ai_review.md")):
                log("Review task failed — PipelineTerminalFail")
                write_failure_report(root, generated_dir, "PipelineTerminalFail", last_verify_report, last_review_report)
                save_json(
                    root,
                    os.path.join(generated_dir, RUN_EVIDENCE_REPORT),
                    collect_run_evidence(root, generated_dir, {**state, "phase": "TerminalFail", "failure_type": "PipelineTerminalFail"}),
                )
                return 1
            last_review_report = os.path.join(generated_dir, REVIEW_REPORT_MD)
            classification = classify_review_issues(root, generated_dir)
            pipeline_issues = classification["pipeline_editable"]
            if pipeline_issues:
                log(f"Review found {len(pipeline_issues)} pipeline_editable issue(s) — not doc rework responsibility")
                for pi in pipeline_issues:
                    issue_text = pi.get("issue", "?") if isinstance(pi, dict) else str(pi)
                    log(f"  pipeline_editable: {issue_text}")
            if classification["has_doc_editable_must_fix"]:
                phase = "ReviewNeedsRework"
            elif classification["has_any_must_fix"] and not classification["has_doc_editable_must_fix"]:
                log("All must-fix items are non-doc-editable (pipeline/ambiguous/stale); skipping rework")
                pipeline_detail = "; ".join(
                    (i.get("issue", "?") if isinstance(i, dict) else str(i))
                    for bucket in ("pipeline_editable", "ambiguous")
                    for i in classification[bucket]
                )
                write_failure_report(
                    root, generated_dir,
                    "PipelineFixNeeded",
                    last_verify_report, last_review_report,
                    unresolved_reason=f"Review must-fix items are not doc-editable: {pipeline_detail}",
                    suggested_next_action="pipeline_fix_or_contract_fix",
                )
                terminal_state = {**state, "phase": "TerminalFail", "failure_type": "PipelineFixNeeded"}
                save_json(root, os.path.join(generated_dir, RUN_EVIDENCE_REPORT), collect_run_evidence(root, generated_dir, terminal_state))
                save_json(root, state_path, terminal_state)
                return 1
            else:
                phase = "Passed"
            continue

        if phase == "ReviewNeedsRework":
            if review_rework_count >= MAX_REVIEW_REWORK or total_repair_actions >= MAX_TOTAL_REPAIR_ACTIONS:
                log("ReviewTerminalFail: rework limit reached")
                write_failure_report(root, generated_dir, "ReviewTerminalFail", last_verify_report, last_review_report)
                terminal_state = {**state, "phase": "TerminalFail", "failure_type": "ReviewTerminalFail"}
                save_json(root, os.path.join(generated_dir, RUN_EVIDENCE_REPORT), collect_run_evidence(root, generated_dir, terminal_state))
                save_json(root, state_path, terminal_state)
                return 1
            log("Running review_rework")
            if not run_codex_task(root, task_path("ai_review_rework.md")):
                log("review_rework task failed — treating as TerminalFail")
                write_failure_report(root, generated_dir, "ReviewTerminalFail", last_verify_report, last_review_report)
                save_json(
                    root,
                    os.path.join(generated_dir, RUN_EVIDENCE_REPORT),
                    collect_run_evidence(root, generated_dir, {**state, "phase": "TerminalFail", "failure_type": "ReviewTerminalFail"}),
                )
                return 1
            rework_report = load_json(root, os.path.join(generated_dir, REVIEW_REWORK_REPORT))
            if rework_report and rework_report.get("unresolved_conflict"):
                reason = rework_report.get("unresolved_reason") or "Rework reported unresolved conflict."
                action = rework_report.get("suggested_next_action") or ""
                log(f"UnresolvedConflict: {reason}")
                write_failure_report(
                    root,
                    generated_dir,
                    "UnresolvedConflictTerminalFail",
                    last_verify_report,
                    last_review_report,
                    unresolved_reason=reason,
                    suggested_next_action=action if action else None,
                )
                save_json(
                    root,
                    os.path.join(generated_dir, RUN_EVIDENCE_REPORT),
                    collect_run_evidence(
                        root,
                        generated_dir,
                        {**state, "phase": "TerminalFail", "failure_type": "UnresolvedConflictTerminalFail"},
                    ),
                )
                save_json(
                    root,
                    state_path,
                    {**state, "phase": "TerminalFail", "failure_type": "UnresolvedConflictTerminalFail"},
                )
                return 1
            review_rework_count += 1
            total_repair_actions += 1
            phase = "ReviewReworking"
            continue

        if phase == "Passed":
            log("Writing run evidence and deterministic E2E summary")
            final_state = {**state, "phase": "Passed"}
            save_json(
                root,
                os.path.join(generated_dir, RUN_EVIDENCE_REPORT),
                collect_run_evidence(root, generated_dir, final_state),
            )
            if not run_render_e2e_summary(root):
                log("render_docgen_e2e_summary failed — PipelineTerminalFail")
                write_failure_report(root, generated_dir, "PipelineTerminalFail", last_verify_report, last_review_report)
                save_json(
                    root,
                    os.path.join(generated_dir, RUN_EVIDENCE_REPORT),
                    collect_run_evidence(
                        root,
                        generated_dir,
                        {**state, "phase": "TerminalFail", "failure_type": "PipelineTerminalFail"},
                    ),
                )
                return 1
            save_json(root, state_path, final_state)
            log("Closed loop completed. See docs/generated/docgen_e2e_summary.md")
            return 0

        log(f"Unknown phase {phase!r} — PipelineTerminalFail")
        write_failure_report(root, generated_dir, "PipelineTerminalFail", last_verify_report, last_review_report)
        save_json(
            root,
            os.path.join(generated_dir, RUN_EVIDENCE_REPORT),
            collect_run_evidence(root, generated_dir, {**state, "phase": "TerminalFail", "failure_type": "PipelineTerminalFail"}),
        )
        return 1


if __name__ == "__main__":
    sys.exit(main())
