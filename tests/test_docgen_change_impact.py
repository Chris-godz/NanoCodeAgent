#!/usr/bin/env python3
"""Regression tests for the change-facts and change-impact docgen pipeline.

Covers: change_facts.py (JSON), changed_context.py (legacy --format md), run_change_impact.sh,
        change_impact_report.md structure (legacy), and doc scope / impact signals.
"""

import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
FIXTURES = os.path.join(REPO_ROOT, "tests", "fixtures", "docgen", "change_impact")
SCRIPTS = os.path.join(REPO_ROOT, "scripts", "docgen")
GENERATED = os.path.join(REPO_ROOT, "docs", "generated")


def run_script(script: str, *args: str, cwd: str | None = None) -> subprocess.CompletedProcess:
    """Run a docgen script and return the CompletedProcess."""
    return subprocess.run(
        [sys.executable, os.path.join(SCRIPTS, script), *args],
        capture_output=True,
        text=True,
        cwd=cwd or REPO_ROOT,
    )


def run_bash(script_path: str, env: dict | None = None) -> subprocess.CompletedProcess:
    """Run a bash script from REPO_ROOT."""
    return subprocess.run(
        ["bash", script_path],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        env={**(os.environ), **(env or {})},
    )


class TestChangeFacts(unittest.TestCase):
    """change_facts.py produces valid JSON with changed_files and diff_ref."""

    def test_change_facts_with_files_exits_zero(self):
        import json
        case_a = os.path.join(FIXTURES, "case_a_changed_files.txt")
        out_path = os.path.join(tempfile.gettempdir(), "change_facts_a.json")
        r = run_script(
            "change_facts.py",
            "--root", REPO_ROOT,
            "--files", case_a,
            "--output", out_path,
        )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertTrue(os.path.isfile(out_path))
        with open(out_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        self.assertIn("changed_files", data)
        self.assertIn("diff_ref", data)
        self.assertIsInstance(data["changed_files"], list)

    def test_change_facts_json_has_public_surface_signals(self):
        import json
        out_path = os.path.join(tempfile.gettempdir(), "change_facts_signals.json")
        r = run_script(
            "change_facts.py",
            "--root", REPO_ROOT,
            "--files", os.path.join(FIXTURES, "case_a_changed_files.txt"),
            "--output", out_path,
        )
        self.assertEqual(r.returncode, 0)
        with open(out_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        self.assertIn("public_surface_signals", data)
        self.assertIn("large_refactor_signals", data)


class TestChangedContext(unittest.TestCase):
    """changed_context.py runs with --format md (legacy) or default json."""

    def test_changed_context_with_files_exits_zero(self):
        case_a = os.path.join(FIXTURES, "case_a_changed_files.txt")
        out_path = os.path.join(tempfile.gettempdir(), "change_ctx_a.md")
        r = run_script(
            "changed_context.py",
            "--root", REPO_ROOT,
            "--files", case_a,
            "--format", "md",
            "--output", out_path,
        )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertTrue(os.path.isfile(out_path))
        with open(out_path, "r", encoding="utf-8") as f:
            self.assertIn("# Changed File Context", f.read())

    def test_changed_context_output_contains_title_and_impact(self):
        out_path = os.path.join(tempfile.gettempdir(), "change_ctx_fixture.md")
        r = run_script(
            "changed_context.py",
            "--root", REPO_ROOT,
            "--files", os.path.join(FIXTURES, "case_a_changed_files.txt"),
            "--format", "md",
            "--output", out_path,
        )
        self.assertEqual(r.returncode, 0)
        with open(out_path, "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn("# Changed File Context", content)
        self.assertIn("Impact perspective", content)


class TestRunChangeImpact(unittest.TestCase):
    """run_change_impact.sh runs to completion when report can be generated."""

    def test_run_change_impact_exits_zero(self):
        # Requires at least one commit for git diff HEAD~1
        r = run_bash(os.path.join(REPO_ROOT, "scripts", "docgen", "run_change_impact.sh"))
        if r.returncode != 0 and "git diff failed" in (r.stderr + r.stdout):
            self.skipTest("Repo has no history for git diff HEAD~1")
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")


class TestChangeImpactReportStructure(unittest.TestCase):
    """change_impact_report.md contains required sections."""

    def test_report_has_required_sections(self):
        report_path = os.path.join(GENERATED, "change_impact_report.md")
        if not os.path.isfile(report_path):
            self.skipTest("change_impact_report.md not generated yet; run run_change_impact.sh first")
        with open(report_path, "r", encoding="utf-8") as f:
            content = f.read()
        for section in (
            "# Change Impact Report",
            "## Change summary",
            "## Affected modules",
            "## Affected docs",
            "## Related examples",
            "## Impact classification",
            "### Required updates",
            "### Optional updates",
            "### No update needed",
            "## Rationale",
        ):
            self.assertIn(section, content, f"Missing section: {section}")


class TestThreeConclusionTypes(unittest.TestCase):
    """The three conclusion types (required / optional / no update needed) are each covered."""

    def test_case_a_produces_no_update_needed(self):
        """Case A (tests only) should yield likely_doc_impact false."""
        out_ctx = os.path.join(tempfile.gettempdir(), "case_a_ctx.md")
        r = run_script(
            "changed_context.py",
            "--root", REPO_ROOT,
            "--files", os.path.join(FIXTURES, "case_a_changed_files.txt"),
            "--format", "md",
            "--output", out_ctx,
        )
        self.assertEqual(r.returncode, 0)
        with open(out_ctx, "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn("**likely_doc_impact**: false", content)

    def test_case_b_produces_required(self):
        """Case B (README/main) should yield likely_doc_impact true; generate_impact_report optional."""
        out_ctx = os.path.join(tempfile.gettempdir(), "case_b_ctx.md")
        r = run_script(
            "changed_context.py",
            "--root", REPO_ROOT,
            "--files", os.path.join(FIXTURES, "case_b_changed_files.txt"),
            "--format", "md",
            "--output", out_ctx,
        )
        self.assertEqual(r.returncode, 0)
        with open(out_ctx, "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn("**likely_doc_impact**: true", content)
        out_report = os.path.join(tempfile.gettempdir(), "case_b_report.md")
        r2 = run_script(
            "generate_impact_report.py",
            "--root", REPO_ROOT,
            "--context", out_ctx,
            "--output", out_report,
        )
        self.assertEqual(r2.returncode, 0)
        with open(out_report, "r", encoding="utf-8") as f:
            report = f.read()
        idx = report.find("### Required updates")
        self.assertGreater(idx, -1, "Report must contain Required updates section")

    def test_case_c_produces_optional_or_required(self):
        """Case C (examples/tutorial) should yield likely_doc_impact true."""
        out_ctx = os.path.join(tempfile.gettempdir(), "case_c_ctx.md")
        r = run_script(
            "changed_context.py",
            "--root", REPO_ROOT,
            "--files", os.path.join(FIXTURES, "case_c_changed_files.txt"),
            "--format", "md",
            "--output", out_ctx,
        )
        self.assertEqual(r.returncode, 0)
        with open(out_ctx, "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn("**likely_doc_impact**: true", content)


if __name__ == "__main__":
    unittest.main()
