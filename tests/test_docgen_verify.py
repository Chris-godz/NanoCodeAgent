#!/usr/bin/env python3
"""Regression tests for the docgen verification scripts.

Covers: verify_paths, verify_links, verify_commands, verify_doc_consistency,
        and doc_inventory staleness output.
"""

import os
import tempfile
import subprocess
import sys
import unittest

from scripts.docgen.repo_map import relative_depth
from scripts.docgen.verify_doc_consistency import extract_filenames

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


class TestVerifyPaths(unittest.TestCase):
    def test_good_doc_passes(self):
        r = run_script(
            "verify_paths.py", os.path.join(FIXTURES, "good.md"), "--root", REPO_ROOT
        )
        self.assertEqual(r.returncode, 0)

    def test_bad_paths_fails(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "bad_paths.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("does_not_exist", r.stdout + r.stderr)

    def test_missing_root_file_fails(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "missing_root_path.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("missing_doc.md", r.stdout + r.stderr)

    def test_bare_code_mentions_are_ignored(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "bare_code_mentions.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")


class TestVerifyLinks(unittest.TestCase):
    def test_good_doc_passes(self):
        r = run_script(
            "verify_links.py", os.path.join(FIXTURES, "good.md"), "--root", REPO_ROOT
        )
        self.assertEqual(r.returncode, 0)

    def test_bad_links_fails(self):
        r = run_script(
            "verify_links.py",
            os.path.join(FIXTURES, "bad_links.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("nonexistent", r.stdout + r.stderr)


class TestVerifyCommands(unittest.TestCase):
    def test_good_doc_passes(self):
        r = run_script(
            "verify_commands.py", os.path.join(FIXTURES, "good.md"), "--root", REPO_ROOT
        )
        self.assertEqual(r.returncode, 0)

    def test_bad_commands_fails(self):
        r = run_script(
            "verify_commands.py",
            os.path.join(FIXTURES, "bad_commands.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")


class TestVerifyDocConsistency(unittest.TestCase):
    def test_extract_filenames_includes_special_root_files(self):
        filenames = extract_filenames(
            "See `.gitignore`, `build.sh`, `AGENTS.md`, and `scripts/docgen/setup.sh`."
        )
        self.assertEqual(
            filenames,
            [".gitignore", "AGENTS.md", "build.sh", "scripts/docgen/setup.sh"],
        )

    def test_extract_filenames_ignores_bare_code_mentions(self):
        filenames = extract_filenames(
            "See `repo_map.py`, `agent_loop.cpp`, and `scripts/docgen/repo_map.py`."
        )
        self.assertEqual(filenames, ["scripts/docgen/repo_map.py"])

    def test_good_doc_passes(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "good.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 0)

    def test_bad_paths_with_scripts_prefix_fails(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "bad_paths.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertIn("fake_script", r.stdout + r.stderr)

    def test_missing_extension_script_reference_fails(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "bad_consistency_special.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertIn("scripts/docgen/fake_tool", r.stdout + r.stderr)


class TestRepoMap(unittest.TestCase):
    def test_relative_depth_counts_from_root(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = os.path.join(temp_dir, "repo")
            nested = os.path.join(root, "docs", "generated")
            os.makedirs(nested)

            self.assertEqual(relative_depth(root, root), 0)
            self.assertEqual(relative_depth(root, os.path.join(root, "docs")), 1)
            self.assertEqual(relative_depth(root, nested), 2)


class TestDocInventory(unittest.TestCase):
    def test_no_unknown_staleness(self):
        r = run_script("doc_inventory.py", "--root", REPO_ROOT)
        self.assertEqual(r.returncode, 0, f"stderr: {r.stderr}")
        # All entries should have a real risk value, not "unknown"
        for line in r.stdout.splitlines():
            if line.startswith("|") and "Staleness" not in line and "---" not in line:
                self.assertNotIn("unknown", line, f"unknown staleness in: {line}")

    def test_source_reference_date_present(self):
        r = run_script("doc_inventory.py", "--root", REPO_ROOT)
        self.assertIn("Source reference date", r.stdout)


if __name__ == "__main__":
    unittest.main()
