#!/usr/bin/env python3
"""Tests for the Docgen E2E pipeline (fixed target docs: README + book).

- No-codex test: run_docgen_e2e.sh must exit non-zero and print a clear codex/required message when codex is not in PATH.
- With codex (optional): full E2E produces docgen_e2e_summary.md with required sections; tests skip if codex unavailable.
- Bootstrap test: change_facts → doc_scope_decision (fixture) → reference_context → run_verify_report without codex.
- Legacy: generate_candidates, run_validation_report, etc. may still work with change_impact_report (reference use).
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SCRIPTS = os.path.join(REPO_ROOT, "scripts", "docgen")
GENERATED = os.path.join(REPO_ROOT, "docs", "generated")
E2E_SUMMARY = os.path.join(GENERATED, "docgen_e2e_summary.md")
CANDIDATES_DIR = os.path.join(GENERATED, "candidates")
CHANGE_FACTS = os.path.join(GENERATED, "change_facts.json")
SCOPE_DECISION = os.path.join(GENERATED, "doc_scope_decision.json")
REFERENCE_CONTEXT = os.path.join(GENERATED, "reference_context.json")
VERIFY_REPORT = os.path.join(GENERATED, "verify_report.json")
IMPACT_REPORT = os.path.join(GENERATED, "change_impact_report.md")
E2E_FIXTURE_REPORT = os.path.join(
    REPO_ROOT, "tests", "fixtures", "docgen", "e2e", "minimal_impact_report.md"
)
E2E_SCRIPT = os.path.join(REPO_ROOT, "scripts", "docgen", "run_docgen_e2e.sh")
FIXTURES = os.path.join(REPO_ROOT, "tests", "fixtures", "docgen", "change_impact")

# Minimal PATH so that `command -v codex` fails (no codex in PATH).
NO_CODEX_PATH = os.pathsep.join(("/usr/bin", "/bin"))


def codex_available() -> bool:
    if os.environ.get("DOCGEN_FORCE_NO_CODEX") == "1":
        return False
    return bool(shutil.which("codex"))


def run_bash(script_path: str, env: dict | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["bash", script_path],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        env={**(os.environ), **(env or {})},
    )


def run_script(script: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, os.path.join(SCRIPTS, script), *args],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
    )


def run_bash_env(
    script_path: str, env: dict | None = None
) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["bash", script_path],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        env={**(os.environ), **(env or {})},
    )


class TestE2ERequiresCodex(unittest.TestCase):
    """When codex is not in PATH, run_docgen_e2e.sh must exit non-zero with a clear message."""

    def test_run_docgen_e2e_fails_without_codex(self):
        env = {**os.environ, "PATH": NO_CODEX_PATH}
        r = run_bash(E2E_SCRIPT, env=env)
        self.assertNotEqual(
            r.returncode,
            0,
            "run_docgen_e2e.sh must exit non-zero when codex is not in PATH",
        )
        combined = (r.stdout or "") + (r.stderr or "")
        self.assertTrue(
            "codex" in combined.lower() or "required" in combined.lower(),
            f"stderr/stdout must mention codex or required; got: {combined!r}",
        )


class TestE2EPipelineSmoke(unittest.TestCase):
    """With codex available, run_docgen_e2e.sh completes and produces docgen_e2e_summary.md with key sections."""

    def test_run_docgen_e2e_produces_summary(self):
        if not codex_available():
            self.skipTest("codex not in PATH; full E2E requires Codex CLI")
        r = run_bash(E2E_SCRIPT)
        if r.returncode != 0 and (
            "git diff" in (r.stderr or "") or "git diff" in (r.stdout or "")
        ):
            self.skipTest(
                "Repo has no history for git diff; run_docgen_e2e requires REF or git history"
            )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertTrue(
            os.path.isfile(E2E_SUMMARY), "docgen_e2e_summary.md should exist"
        )

    def test_e2e_summary_has_required_sections(self):
        if not os.path.isfile(E2E_SUMMARY):
            self.skipTest(
                "Run run_docgen_e2e.sh first (with codex) to generate docgen_e2e_summary.md"
            )
        with open(E2E_SUMMARY, "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn(
            "Final recommendation", content, "Summary must contain final recommendation"
        )
        self.assertIn(
            "Evidence sources", content, "Evidence-driven summary must list sources"
        )
        self.assertIn(
            "change_facts", content, "Summary must reflect change_facts evidence"
        )
        self.assertIn(
            "Validation", content, "Summary must contain validation/verify section"
        )
        self.assertIn("Review", content, "Summary must contain review section")


class TestDocgenBootstrap(unittest.TestCase):
    """Bootstrap test: change_facts → scope fixture → reference_context → run_verify_report (no codex)."""

    def test_change_facts_produces_json(self):
        """Running change_facts with --files fixture produces valid change_facts.json."""
        case_a = os.path.join(FIXTURES, "case_a_changed_files.txt")
        self.assertTrue(os.path.isfile(case_a))
        out = os.path.join(tempfile.gettempdir(), "bootstrap_facts.json")
        r = run_script(
            "change_facts.py", "--root", REPO_ROOT, "--files", case_a, "--output", out
        )
        self.assertEqual(r.returncode, 0, f"stderr: {r.stderr}")
        self.assertTrue(os.path.isfile(out))
        with open(out, "r", encoding="utf-8") as f:
            data = json.load(f)
        self.assertIn("changed_files", data)
        self.assertIn("diff_ref", data)

    def test_reference_context_after_scope_fixture(self):
        """With a minimal doc_scope_decision.json, reference_context.py runs and writes reference_context.json."""
        with tempfile.TemporaryDirectory() as tmp:
            facts_path = os.path.join(tmp, "change_facts.json")
            scope_path = os.path.join(tmp, "doc_scope_decision.json")
            ref_path = os.path.join(tmp, "reference_context.json")
            run_script(
                "change_facts.py",
                "--root",
                REPO_ROOT,
                "--files",
                os.path.join(FIXTURES, "case_a_changed_files.txt"),
                "--output",
                facts_path,
            )
            with open(scope_path, "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "update_readme": False,
                        "update_book": False,
                        "book_dirs_or_chapters": [],
                        "rationale": "Bootstrap test: no doc update.",
                    },
                    f,
                    indent=2,
                )
            r = run_script(
                "reference_context.py",
                "--root",
                REPO_ROOT,
                "--scope",
                scope_path,
                "--facts",
                facts_path,
                "--output",
                ref_path,
            )
            self.assertEqual(r.returncode, 0, f"stderr: {r.stderr}")
            self.assertTrue(os.path.isfile(ref_path))
            with open(ref_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.assertIn("examples", data)
            self.assertIn("source_paths", data)

    def test_run_verify_report_with_scope_fixture(self):
        """run_verify_report.py runs with scope fixture and produces verify_report.json."""
        with tempfile.TemporaryDirectory() as tmp:
            scope_path = os.path.join(tmp, "doc_scope_decision.json")
            report_json = os.path.join(tmp, "verify_report.json")
            with open(scope_path, "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "update_readme": True,
                        "update_book": False,
                        "book_dirs_or_chapters": [],
                        "rationale": "Bootstrap: verify README only.",
                    },
                    f,
                    indent=2,
                )
            r = run_script(
                "run_verify_report.py",
                "--root",
                REPO_ROOT,
                "--scope",
                scope_path,
                "--report-json",
                report_json,
            )
            self.assertEqual(r.returncode, 0, f"stderr: {r.stderr}")
            self.assertTrue(os.path.isfile(report_json))
            with open(report_json, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.assertIn("blocking_passed", data)
            self.assertIn("target_docs_checked", data)

    def test_run_verify_report_can_ignore_generated_path_prefixes(self):
        with tempfile.TemporaryDirectory() as tmp:
            readme_path = os.path.join(tmp, "README.md")
            with open(readme_path, "w", encoding="utf-8") as f:
                f.write(
                    "# Temp README\n\n"
                    "See `docs/generated/change_facts.json` for generated evidence.\n"
                )

            scope_path = os.path.join(tmp, "doc_scope_decision.json")
            report_json = os.path.join(tmp, "verify_report.json")
            with open(scope_path, "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "update_readme": True,
                        "update_book": False,
                        "book_dirs_or_chapters": [],
                        "approved_targets": ["README.md"],
                        "rationale": "Bootstrap: verify README only.",
                    },
                    f,
                    indent=2,
                )

            failing = run_script(
                "run_verify_report.py",
                "--root",
                tmp,
                "--scope",
                scope_path,
                "--report-json",
                report_json,
            )
            self.assertNotEqual(failing.returncode, 0)

            passing = run_script(
                "run_verify_report.py",
                "--root",
                tmp,
                "--scope",
                scope_path,
                "--verify-paths-ignore-repo-prefix",
                "docs/generated/",
                "--report-json",
                report_json,
            )
            self.assertEqual(passing.returncode, 0, f"stderr: {passing.stderr}")
            with open(report_json, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.assertTrue(data["blocking_passed"], data)

    def test_run_verify_report_records_mermaid_artifacts_for_book_doc(self):
        """verify_report.json should carry Mermaid artifact reports, and screenshot artifacts should exist."""
        with tempfile.TemporaryDirectory() as tmp:
            book_src = os.path.join(tmp, "book", "src")
            os.makedirs(book_src, exist_ok=True)
            chapter_path = os.path.join(book_src, "diagram-demo.md")
            with open(chapter_path, "w", encoding="utf-8") as f:
                f.write(
                    "# Diagram Demo\n\n"
                    "```mermaid\n"
                    "flowchart LR\n"
                    "    User[User] --> Agent[Agent]\n"
                    "```\n"
                )
            scope_path = os.path.join(tmp, "doc_scope_decision.json")
            report_json = os.path.join(tmp, "verify_report.json")
            with open(scope_path, "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "update_readme": False,
                        "update_book": True,
                        "book_dirs_or_chapters": ["book/src/diagram-demo.md"],
                        "approved_targets": ["book/src/diagram-demo.md"],
                        "rationale": "Bootstrap: verify a Mermaid-bearing book chapter.",
                    },
                    f,
                    indent=2,
                )
            spec_path = os.path.join(
                tmp,
                "docs",
                "generated",
                "diagram_specs",
                "book__src__diagram-demo",
                "block-01.md",
            )
            os.makedirs(os.path.dirname(spec_path), exist_ok=True)
            with open(spec_path, "w", encoding="utf-8") as f:
                f.write("# Diagram Spec\n")
            r = run_script(
                "run_verify_report.py",
                "--root",
                tmp,
                "--scope",
                scope_path,
                "--report-json",
                report_json,
            )
            self.assertEqual(r.returncode, 0, f"stderr: {r.stderr}")
            with open(report_json, "r", encoding="utf-8") as f:
                data = json.load(f)
            per_file = data.get("per_file", [])
            self.assertEqual(len(per_file), 1, per_file)
            spec_checks = [
                check
                for check in per_file[0].get("checks", [])
                if check.get("name") == "verify_diagram_specs.py"
            ]
            self.assertEqual(len(spec_checks), 1, spec_checks)
            self.assertTrue(spec_checks[0].get("passed"), spec_checks[0])
            mermaid_checks = [
                check
                for check in per_file[0].get("checks", [])
                if check.get("name") == "verify_mermaid.py"
            ]
            self.assertEqual(len(mermaid_checks), 1, mermaid_checks)
            artifact_report = mermaid_checks[0].get("artifact_report")
            self.assertTrue(artifact_report, mermaid_checks[0])
            artifact_report_path = os.path.join(tmp, artifact_report)
            self.assertTrue(os.path.isfile(artifact_report_path), artifact_report_path)
            with open(artifact_report_path, "r", encoding="utf-8") as f:
                artifact_data = json.load(f)
            self.assertTrue(artifact_data.get("blocks"), artifact_data)
            screenshot_path = artifact_data["blocks"][0].get("screenshot_path")
            self.assertTrue(screenshot_path, artifact_data)
            self.assertTrue(
                os.path.isfile(os.path.join(tmp, screenshot_path)), screenshot_path
            )

    def test_run_verify_report_fails_when_mermaid_doc_has_no_spec(self):
        """Mermaid-bearing target docs should fail blocking verify when diagram specs are missing."""
        with tempfile.TemporaryDirectory() as tmp:
            book_src = os.path.join(tmp, "book", "src")
            os.makedirs(book_src, exist_ok=True)
            chapter_path = os.path.join(book_src, "diagram-demo.md")
            with open(chapter_path, "w", encoding="utf-8") as f:
                f.write(
                    "# Diagram Demo\n\n"
                    "```mermaid\n"
                    "flowchart LR\n"
                    "    User[User] --> Agent[Agent]\n"
                    "```\n"
                )
            scope_path = os.path.join(tmp, "doc_scope_decision.json")
            report_json = os.path.join(tmp, "verify_report.json")
            with open(scope_path, "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "update_readme": False,
                        "update_book": True,
                        "book_dirs_or_chapters": ["book/src/diagram-demo.md"],
                        "approved_targets": ["book/src/diagram-demo.md"],
                        "rationale": "Bootstrap: missing spec should block verify.",
                    },
                    f,
                    indent=2,
                )
            r = run_script(
                "run_verify_report.py",
                "--root",
                tmp,
                "--scope",
                scope_path,
                "--report-json",
                report_json,
            )
            self.assertNotEqual(r.returncode, 0)
            with open(report_json, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.assertFalse(data["blocking_passed"])
            self.assertTrue(
                any(
                    issue.get("type") == "missing_diagram_spec"
                    for issue in data.get("blocking_issues", [])
                ),
                data,
            )


class TestE2EWithExistingImpactReport(unittest.TestCase):
    """With change_impact_report.md present, downstream steps produce validation, review, summary."""

    def test_generate_candidates_exits_zero(self):
        if not os.path.isfile(IMPACT_REPORT):
            self.skipTest("change_impact_report.md not found; run change impact first")
        r = run_script(
            "generate_candidates.py",
            "--root",
            REPO_ROOT,
            "--report",
            IMPACT_REPORT,
            "--out-dir",
            CANDIDATES_DIR,
        )
        self.assertEqual(r.returncode, 0, f"stderr: {r.stderr}")

    def test_validation_report_generated(self):
        if not os.path.isfile(IMPACT_REPORT):
            self.skipTest("change_impact_report.md not found")
        with tempfile.TemporaryDirectory() as tmp:
            candidates_tmp = os.path.join(tmp, "candidates")
            run_script(
                "generate_candidates.py",
                "--root",
                REPO_ROOT,
                "--report",
                IMPACT_REPORT,
                "--out-dir",
                candidates_tmp,
            )
            report_tmp = os.path.join(tmp, "docgen_validation_report.md")
            r = run_script(
                "run_validation_report.py",
                "--root",
                REPO_ROOT,
                "--candidates-dir",
                candidates_tmp,
                "--report",
                report_tmp,
            )
            self.assertEqual(
                r.returncode,
                0,
                f"blocking validation should pass when only impact report is checked: {r.stderr}",
            )
            self.assertTrue(
                os.path.isfile(report_tmp), "validation report should be written"
            )

    def test_review_report_generated(self):
        if not os.path.isfile(IMPACT_REPORT):
            self.skipTest("change_impact_report.md not found")
        with tempfile.TemporaryDirectory() as tmp:
            candidates_tmp = os.path.join(tmp, "candidates")
            run_script(
                "generate_candidates.py",
                "--root",
                REPO_ROOT,
                "--report",
                IMPACT_REPORT,
                "--out-dir",
                candidates_tmp,
            )
            review_tmp = os.path.join(tmp, "docgen_review_report.md")
            r = run_script(
                "run_review_report.py",
                "--root",
                REPO_ROOT,
                "--candidates-dir",
                candidates_tmp,
                "--impact-report",
                IMPACT_REPORT,
                "--output",
                review_tmp,
            )
            self.assertEqual(r.returncode, 0)
            self.assertTrue(os.path.isfile(review_tmp))

    def test_e2e_summary_generated(self):
        # Legacy: generate_e2e_summary.py was removed; E2E summary is now produced by
        # render_docgen_e2e_summary.py in the main path. This test is skipped.
        self.skipTest(
            "generate_e2e_summary.py removed; summary from render_docgen_e2e_summary.py (see test_docgen_e2e_loop)"
        )


class TestE2EFixtureDriven(unittest.TestCase):
    """Fixture-driven E2E: impact report with README/book paths yields candidates."""

    def test_fixture_report_generates_candidates(self):
        self.assertTrue(os.path.isfile(E2E_FIXTURE_REPORT))
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = os.path.join(tmp, "candidates")
            r = run_script(
                "generate_candidates.py",
                "--root",
                REPO_ROOT,
                "--report",
                E2E_FIXTURE_REPORT,
                "--out-dir",
                out_dir,
            )
            self.assertEqual(r.returncode, 0, f"stderr: {r.stderr}")
            candidates = (
                [f for f in os.listdir(out_dir) if f.endswith(".md")]
                if os.path.isdir(out_dir)
                else []
            )
            self.assertGreaterEqual(
                len(candidates),
                1,
                "Fixture report should produce at least one candidate (README.md or book_src_01-overview.md)",
            )
            self.assertTrue(
                any("README" in c or "01_overview" in c for c in candidates),
                f"Expected README or book overview candidate, got: {candidates}",
            )


if __name__ == "__main__":
    unittest.main()
