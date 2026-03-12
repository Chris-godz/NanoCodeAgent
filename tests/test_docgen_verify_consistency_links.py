#!/usr/bin/env python3
"""Regression tests for verify_doc_consistency link-target handling."""

import os
import subprocess
import sys
import unittest


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
FIXTURES = os.path.join(REPO_ROOT, "tests", "fixtures", "docgen")
SCRIPTS = os.path.join(REPO_ROOT, "scripts", "docgen")


def run_script(script: str, *args: str) -> subprocess.CompletedProcess:
    """Run a docgen script and return the CompletedProcess."""
    return subprocess.run(
        [sys.executable, os.path.join(SCRIPTS, script), *args],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
    )


class TestVerifyDocConsistencyLinks(unittest.TestCase):
    def test_missing_script_markdown_link_fails(self):
        result = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "bad_consistency_links.md"),
            "--root",
            REPO_ROOT,
        )

        self.assertEqual(
            result.returncode, 1, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        self.assertIn(
            "scripts/docgen/fake_link_target.py", result.stdout + result.stderr
        )


if __name__ == "__main__":
    unittest.main()
