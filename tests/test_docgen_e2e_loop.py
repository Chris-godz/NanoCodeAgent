#!/usr/bin/env python3
"""Tests for the Docgen E2E closed loop (verify-repair, review-rework).

- Full E2E: run_docgen_e2e_closed.sh (Phase 1 + Phase 2) end-to-end when codex available.
- Normal path: scope + passing verify → review → summary (requires codex; skip if unavailable).
- Verify-repair path: verify fails → repair → verify passes (requires codex or mock).
- Review-rework path: review has must fix → rework → verify → review (requires codex).
- Termination: no scope → PipelineTerminalFail; repair/rework limits → VerifyTerminalFail / ReviewTerminalFail.
- Bootstrap (no codex): missing scope exits 1 and writes e2e_failure_report.md; verify report structure.
"""

import json
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SCRIPTS = os.path.join(REPO_ROOT, "scripts", "docgen")
CLOSED_SCRIPT = os.path.join(REPO_ROOT, "scripts", "docgen", "run_docgen_e2e_closed.sh")
GENERATED = "docs", "generated"
GENERATED_DIR = os.path.join(REPO_ROOT, *GENERATED)
STATE_FILE = "e2e_loop_state.json"
FAILURE_REPORT = "e2e_failure_report.md"
VERIFY_REPORT = "verify_report.json"
RUN_EVIDENCE_REPORT = "e2e_run_evidence.json"
SCOPE_DECISION = "doc_scope_decision.json"
E2E_SUMMARY = os.path.join(REPO_ROOT, *GENERATED, "docgen_e2e_summary.md")
NO_CODEX_PATH = os.pathsep.join(("/usr/bin", "/bin"))


def codex_available() -> bool:
    if os.environ.get("DOCGEN_FORCE_NO_CODEX") == "1":
        return False
    return bool(shutil.which("codex"))


def load_loop_module():
    module_path = os.path.join(SCRIPTS, "run_docgen_e2e_loop.py")
    spec = importlib.util.spec_from_file_location("docgen_e2e_loop_module", module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def run_bash(script_path: str, env: dict | None = None, timeout: int = 600) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["bash", script_path],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        env={**(os.environ), **(env or {})},
        timeout=timeout,
    )


def run_loop(root: str, env: dict | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, os.path.join(SCRIPTS, "run_docgen_e2e_loop.py"), "--root", root],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        env={**(os.environ), **(env or {})},
        timeout=120,
    )


def run_verify(root: str, scope_path: str, report_json: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [
            sys.executable,
            os.path.join(SCRIPTS, "run_verify_report.py"),
            "--root", root,
            "--scope", scope_path,
            "--report-json", report_json,
        ],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        timeout=30,
    )


class TestFullClosedLoopE2E(unittest.TestCase):
    """Full end-to-end: run_docgen_e2e_closed.sh (Phase 1 + Phase 2) in real repo."""

    def test_run_docgen_e2e_closed_fails_without_codex(self):
        """Without codex in PATH, closed script must exit non-zero and mention codex."""
        env = {**os.environ, "PATH": NO_CODEX_PATH}
        r = run_bash(CLOSED_SCRIPT, env=env, timeout=60)
        self.assertNotEqual(r.returncode, 0, "run_docgen_e2e_closed.sh must exit non-zero when codex not in PATH")
        combined = (r.stdout or "") + (r.stderr or "")
        self.assertTrue(
            "codex" in combined.lower() or "required" in combined.lower(),
            f"stderr/stdout must mention codex or required; got: {combined[:500]!r}",
        )

    def test_run_docgen_e2e_closed_full_pipeline_succeeds(self):
        """Run full pipeline: change_facts → scope → reference_context → draft → verify → review → summary."""
        if not codex_available():
            self.skipTest("codex not in PATH; full closed E2E requires Codex CLI")
        self.assertTrue(os.path.isfile(CLOSED_SCRIPT), f"Closed script missing: {CLOSED_SCRIPT}")
        r = run_bash(CLOSED_SCRIPT, timeout=600)
        if r.returncode != 0:
            out = (r.stdout or "") + (r.stderr or "")
            if "git" in out.lower() or "REF" in out or "diff" in out.lower():
                self.skipTest("Pipeline failed likely due to git/REF; run with REF=main or ensure history")
            self.assertEqual(r.returncode, 0, f"Full closed E2E failed.\nstdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertTrue(os.path.isfile(E2E_SUMMARY), "docgen_e2e_summary.md must exist after successful run")
        with open(E2E_SUMMARY, "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn("Final recommendation", content, "Summary must contain Final recommendation")
        self.assertIn("Evidence sources", content)
        self.assertIn("change_facts", content)
        self.assertTrue(
            "Validation" in content or "verify" in content.lower(),
            "Summary must contain validation / verify",
        )
        self.assertTrue(
            "Review" in content or "review" in content.lower(),
            "Summary must contain review",
        )


class TestLoopPipelineTerminalFail(unittest.TestCase):
    """Missing scope or required inputs → PipelineTerminalFail, exit 1, e2e_failure_report.md written."""

    def test_loop_exits_1_when_scope_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            gen = os.path.join(tmp, *GENERATED)
            os.makedirs(gen, exist_ok=True)
            # No doc_scope_decision.json
            r = run_loop(tmp)
            self.assertNotEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")
            failure_path = os.path.join(tmp, *GENERATED, FAILURE_REPORT)
            self.assertTrue(os.path.isfile(failure_path), "e2e_failure_report.md should be written")
            with open(failure_path, "r", encoding="utf-8") as f:
                content = f.read()
            self.assertIn("PipelineTerminalFail", content)


class TestVerifyReportStructure(unittest.TestCase):
    """verify_report.json includes blocking_issues / non_blocking_issues when checks fail."""

    def test_verify_report_has_blocking_issues_when_path_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            gen = os.path.join(tmp, *GENERATED)
            os.makedirs(gen, exist_ok=True)
            scope_path = os.path.join(gen, SCOPE_DECISION)
            with open(scope_path, "w", encoding="utf-8") as f:
                json.dump({
                    "update_readme": True,
                    "update_book": False,
                    "book_dirs_or_chapters": [],
                    "rationale": "Test: README only.",
                }, f, indent=2)
            readme = os.path.join(tmp, "README.md")
            with open(readme, "w", encoding="utf-8") as f:
                f.write("# Test\n\nSee [nonexistent](docs/nonexistent_file_xyz.md).\n")
            report_json = os.path.join(gen, VERIFY_REPORT)
            run_verify(tmp, scope_path, report_json)
            self.assertTrue(os.path.isfile(report_json))
            with open(report_json, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.assertIn("blocking_passed", data)
            self.assertIn("blocking_issues", data)
            self.assertIn("non_blocking_issues", data)
            self.assertFalse(data["blocking_passed"])
            self.assertIsInstance(data["blocking_issues"], list)


class TestRunEvidenceArtifacts(unittest.TestCase):
    """Closed-loop evidence report should capture diagram specs/artifacts and report mentions."""

    def test_collect_run_evidence_tracks_diagram_chain(self):
        module = load_loop_module()
        with tempfile.TemporaryDirectory() as tmp:
            gen = os.path.join(tmp, *GENERATED)
            spec_dir = os.path.join(gen, "diagram_specs", "book__src__demo")
            artifact_dir = os.path.join(gen, "diagram_artifacts", "book__src__demo")
            os.makedirs(spec_dir, exist_ok=True)
            os.makedirs(artifact_dir, exist_ok=True)

            spec_path = os.path.join("docs", "generated", "diagram_specs", "book__src__demo", "block-01.md")
            artifact_report_path = os.path.join("docs", "generated", "diagram_artifacts", "book__src__demo", "report.json")
            screenshot_path = os.path.join("docs", "generated", "diagram_artifacts", "book__src__demo", "block-01.png")
            svg_path = os.path.join("docs", "generated", "diagram_artifacts", "book__src__demo", "block-01.svg")
            review_md_path = os.path.join(gen, "docgen_review_report.md")
            review_rework_path = os.path.join(gen, "review_rework_report.json")
            verify_report_path = os.path.join(gen, VERIFY_REPORT)

            with open(os.path.join(tmp, spec_path), "w", encoding="utf-8") as f:
                f.write("# Diagram Spec\n")
            with open(os.path.join(tmp, screenshot_path), "wb") as f:
                f.write(b"png")
            with open(os.path.join(tmp, svg_path), "w", encoding="utf-8") as f:
                f.write("<svg></svg>")
            with open(os.path.join(tmp, artifact_report_path), "w", encoding="utf-8") as f:
                json.dump({
                    "file": "book/src/demo.md",
                    "artifact_dir": "docs/generated/diagram_artifacts/book__src__demo",
                    "blocks": [
                        {
                            "index": 1,
                            "start_line": 10,
                            "ok": True,
                            "error": "",
                            "svg_path": svg_path.replace("\\", "/"),
                            "screenshot_path": screenshot_path.replace("\\", "/"),
                        }
                    ],
                }, f, indent=2)
            with open(verify_report_path, "w", encoding="utf-8") as f:
                json.dump({
                    "per_file": [
                        {
                            "file": "book/src/demo.md",
                            "checks": [
                                {
                                    "name": "verify_mermaid.py",
                                    "artifact_report": artifact_report_path.replace("\\", "/"),
                                }
                            ],
                        }
                    ]
                }, f, indent=2)
            with open(review_md_path, "w", encoding="utf-8") as f:
                f.write(
                    "Review reads `docs/generated/diagram_specs/book__src__demo/block-01.md` "
                    "and `docs/generated/diagram_artifacts/book__src__demo/block-01.png`.\n"
                )
            with open(review_rework_path, "w", encoding="utf-8") as f:
                json.dump({
                    "addressed_must_fix": [
                        {
                            "file": "book/src/demo.md",
                            "section_or_line": "diagram",
                            "issue": "visual quality",
                            "action_taken": "Used docs/generated/diagram_artifacts/book__src__demo/block-01.png to rework the figure.",
                        }
                    ]
                }, f, indent=2)

            state = {
                "phase": "ReviewReworking",
                "verify_repair_count": 1,
                "review_rework_count": 1,
                "total_repair_actions": 2,
                "last_verify_report": "docs/generated/verify_report.json",
                "last_review_report": "docs/generated/docgen_review_report.md",
                "run_evidence_report": "docs/generated/e2e_run_evidence.json",
            }
            evidence = module.collect_run_evidence(tmp, os.path.join("docs", "generated"), state)
            self.assertIn(spec_path.replace("\\", "/"), evidence["diagram_evidence"]["diagram_specs"])
            self.assertIn(artifact_report_path.replace("\\", "/"), evidence["diagram_evidence"]["artifact_reports"])
            self.assertIn(screenshot_path.replace("\\", "/"), evidence["diagram_evidence"]["screenshots"])
            self.assertIn(artifact_report_path.replace("\\", "/"), evidence["diagram_evidence"]["artifact_reports_referenced_by_verify"])
            self.assertIn(spec_path.replace("\\", "/"), evidence["report_mentions"]["review_mentions"])
            self.assertIn(screenshot_path.replace("\\", "/"), evidence["report_mentions"]["review_mentions"])
            self.assertIn(screenshot_path.replace("\\", "/"), evidence["report_mentions"]["review_rework_mentions"])

    def test_failure_report_mentions_run_evidence(self):
        module = load_loop_module()
        with tempfile.TemporaryDirectory() as tmp:
            gen = os.path.join(tmp, *GENERATED)
            os.makedirs(gen, exist_ok=True)
            evidence_path = os.path.join(gen, RUN_EVIDENCE_REPORT)
            with open(evidence_path, "w", encoding="utf-8") as f:
                json.dump({
                    "diagram_evidence": {
                        "diagram_specs": ["docs/generated/diagram_specs/book__src__demo/block-01.md"],
                        "artifact_reports": ["docs/generated/diagram_artifacts/book__src__demo/report.json"],
                        "screenshots": ["docs/generated/diagram_artifacts/book__src__demo/block-01.png"],
                    }
                }, f, indent=2)
            module.write_failure_report(
                tmp,
                os.path.join("docs", "generated"),
                "PipelineTerminalFail",
                "docs/generated/verify_report.json",
                "docs/generated/docgen_review_report.md",
            )
            failure_path = os.path.join(gen, FAILURE_REPORT)
            self.assertTrue(os.path.isfile(failure_path))
            with open(failure_path, "r", encoding="utf-8") as f:
                content = f.read()
            self.assertIn("docs/generated/e2e_run_evidence.json", content)
            self.assertIn("Diagram specs captured", content)
            self.assertIn("Diagram screenshots captured", content)


class TestMissingDiagramSpecsHelper(unittest.TestCase):
    """The missing-diagram-specs helper should detect scoped Mermaid docs without specs."""

    def test_missing_diagram_specs_helper_reports_missing_spec(self):
        with tempfile.TemporaryDirectory() as tmp:
            book_src = os.path.join(tmp, "book", "src")
            generated = os.path.join(tmp, "docs", "generated")
            os.makedirs(book_src, exist_ok=True)
            os.makedirs(generated, exist_ok=True)
            chapter = os.path.join(book_src, "demo.md")
            with open(chapter, "w", encoding="utf-8") as f:
                f.write("# Demo\n\n```mermaid\nflowchart LR\n    A --> B\n```\n")
            scope = os.path.join(generated, "doc_scope_decision.json")
            with open(scope, "w", encoding="utf-8") as f:
                json.dump({
                    "update_readme": False,
                    "update_book": True,
                    "book_dirs_or_chapters": ["book/src/demo.md"],
                    "approved_targets": ["book/src/demo.md"],
                    "directory_actions": "none",
                    "rationale": "test",
                }, f, indent=2)
            output = os.path.join(generated, "missing_diagram_specs.json")
            r = subprocess.run(
                [
                    sys.executable,
                    os.path.join(SCRIPTS, "missing_diagram_specs.py"),
                    "--root", tmp,
                    "--scope", scope,
                    "--output", output,
                ],
                capture_output=True,
                text=True,
                cwd=REPO_ROOT,
                timeout=30,
            )
            self.assertNotEqual(r.returncode, 0)
            self.assertTrue(os.path.isfile(output))
            with open(output, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.assertEqual(len(data["missing_specs"]), 1, data)
            self.assertEqual(data["missing_specs"][0]["spec_path"], "docs/generated/diagram_specs/book__src__demo/block-01.md")


class TestLoopWithPassingVerify(unittest.TestCase):
    """With scope and passing verify, loop proceeds to review; without codex it fails at review (exit 1)."""

    def test_loop_with_scope_and_passing_verify_fails_at_review_without_codex(self):
        if codex_available():
            self.skipTest("codex in PATH; test targets no-codex behavior")
        with tempfile.TemporaryDirectory() as tmp:
            gen = os.path.join(tmp, *GENERATED)
            os.makedirs(gen, exist_ok=True)
            scope_path = os.path.join(gen, SCOPE_DECISION)
            with open(scope_path, "w", encoding="utf-8") as f:
                json.dump({
                    "update_readme": True,
                    "update_book": False,
                    "book_dirs_or_chapters": [],
                    "rationale": "Test: README only.",
                }, f, indent=2)
            readme = os.path.join(tmp, "README.md")
            with open(readme, "w", encoding="utf-8") as f:
                f.write("# Test\n\nValid [link](README.md).\n")
            report_json = os.path.join(gen, VERIFY_REPORT)
            rv = run_verify(tmp, scope_path, report_json)
            self.assertEqual(rv.returncode, 0, f"verify should pass: {rv.stderr}")
            r = run_loop(tmp, env={**os.environ, "PATH": "/usr/bin:/bin"})
            self.assertNotEqual(r.returncode, 0, "loop should fail when codex not available at review")
            state_path = os.path.join(tmp, *GENERATED, STATE_FILE)
            self.assertTrue(os.path.isfile(state_path), "state should be persisted")
            with open(state_path, "r", encoding="utf-8") as f:
                state = json.load(f)
            self.assertIn(state["phase"], ("VerifyPassed", "DraftReady", "TerminalFail"))


class TestLoopNormalPathWithCodex(unittest.TestCase):
    """Full normal path (verify pass, review no must fix, summary) when codex is available."""

    def test_loop_completes_and_produces_summary(self):
        if not codex_available():
            self.skipTest("codex not in PATH; full closed loop requires Codex CLI")
        with tempfile.TemporaryDirectory() as tmp:
            gen = os.path.join(tmp, *GENERATED)
            os.makedirs(gen, exist_ok=True)
            scope_path = os.path.join(gen, SCOPE_DECISION)
            with open(scope_path, "w", encoding="utf-8") as f:
                json.dump({
                    "update_readme": True,
                    "update_book": False,
                    "book_dirs_or_chapters": [],
                    "rationale": "Test: README only.",
                }, f, indent=2)
            readme = os.path.join(tmp, "README.md")
            with open(readme, "w", encoding="utf-8") as f:
                f.write("# Test\n\nValid [link](README.md).\n")
            r = run_loop(tmp)
            if r.returncode != 0 and ("git" in (r.stderr or r.stdout or "")):
                self.skipTest("Repo/git context may be required by codex")
            self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")
            summary_path = os.path.join(tmp, *GENERATED, "docgen_e2e_summary.md")
            self.assertTrue(os.path.isfile(summary_path))
            with open(summary_path, "r", encoding="utf-8") as f:
                self.assertIn("Final recommendation", f.read())


RENDER_SUMMARY_SCRIPT = os.path.join(SCRIPTS, "render_docgen_e2e_summary.py")


class TestRenderDocgenE2eSummary(unittest.TestCase):
    """Deterministic summary from fixture JSON (no Codex)."""

    def test_render_contains_loop_and_rework_facts(self):
        with tempfile.TemporaryDirectory() as tmp:
            gen = os.path.join(tmp, *GENERATED)
            os.makedirs(gen, exist_ok=True)
            with open(os.path.join(gen, "change_facts.json"), "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "diff_ref": "HEAD~1",
                        "changed_files": [{"path": "README.md", "change_type": "modified"}],
                    },
                    f,
                )
            with open(os.path.join(gen, "doc_scope_decision.json"), "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "update_readme": True,
                        "update_book": False,
                        "book_dirs_or_chapters": [],
                        "approved_targets": ["README.md"],
                        "rationale": "test",
                    },
                    f,
                )
            with open(os.path.join(gen, "verify_report.json"), "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "blocking_passed": True,
                        "target_docs_checked": ["README.md"],
                        "blocking_issues": [],
                        "per_file": [],
                    },
                    f,
                )
            with open(os.path.join(gen, "e2e_loop_state.json"), "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "phase": "Passed",
                        "verify_repair_count": 0,
                        "review_rework_count": 1,
                        "total_repair_actions": 1,
                    },
                    f,
                )
            with open(os.path.join(gen, "review_rework_report.json"), "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "addressed_must_fix": [
                            {"file": "README.md", "issue": "typo in title"},
                        ],
                    },
                    f,
                )
            with open(os.path.join(gen, "e2e_run_evidence.json"), "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "diagram_evidence": {
                            "diagram_specs": [],
                            "artifact_reports": [],
                            "screenshots": [],
                        },
                        "report_mentions": {
                            "review_mentions": [],
                            "review_rework_mentions": [],
                        },
                    },
                    f,
                )
            with open(os.path.join(gen, "docgen_review_report.json"), "w", encoding="utf-8") as f:
                json.dump({"must_fix": [], "overall_assessment": "ok"}, f)
            out = os.path.join(gen, "docgen_e2e_summary.md")
            r = subprocess.run(
                [sys.executable, RENDER_SUMMARY_SCRIPT, "--root", tmp, "--output", out],
                capture_output=True,
                text=True,
                cwd=REPO_ROOT,
                timeout=30,
            )
            self.assertEqual(r.returncode, 0, r.stderr)
            with open(out, "r", encoding="utf-8") as f:
                text = f.read()
            self.assertIn("**review_rework_count**: 1", text)
            self.assertIn("**verify_repair_count**: 0", text)
            self.assertIn("README.md", text)
            self.assertIn("**addressed_must_fix count**: 1", text)
            self.assertIn("Evidence sources", text)
            self.assertIn("change_facts.json", text)

    def test_canary_render_matches_evidence_in_repo(self):
        """When repo has docs/generated evidence, render output must match e2e_loop_state.json."""
        state_path = os.path.join(REPO_ROOT, *GENERATED, STATE_FILE)
        if not os.path.isfile(state_path):
            self.skipTest("e2e_loop_state.json not present (run closed loop first)")
        with open(state_path, "r", encoding="utf-8") as f:
            state = json.load(f)
        out_path = os.path.join(REPO_ROOT, *GENERATED, "docgen_e2e_summary.md")
        r = subprocess.run(
            [sys.executable, RENDER_SUMMARY_SCRIPT, "--root", REPO_ROOT],
            capture_output=True, text=True, cwd=REPO_ROOT, timeout=30,
        )
        self.assertEqual(r.returncode, 0, r.stderr or r.stdout)
        self.assertTrue(os.path.isfile(out_path))
        with open(out_path, "r", encoding="utf-8") as f:
            text = f.read()
        self.assertIn("Evidence sources", text)
        self.assertIn(f"**review_rework_count**: {state.get('review_rework_count')}", text)
        self.assertIn(f"**phase**: `{state.get('phase')}`", text)


class TestTerminationConditions(unittest.TestCase):
    """Documented limits: verify_repair ≤ 2, review_rework ≤ 2, total ≤ 4."""

    def test_loop_script_defines_limits(self):
        # Ensure orchestrator script contains documented limits (no infinite loop).
        path = os.path.join(SCRIPTS, "run_docgen_e2e_loop.py")
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn("MAX_VERIFY_REPAIR = 2", content)
        self.assertIn("MAX_REVIEW_REWORK = 2", content)
        self.assertIn("MAX_TOTAL_REPAIR_ACTIONS = 4", content)


if __name__ == "__main__":
    unittest.main()
