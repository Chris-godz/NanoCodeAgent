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

from scripts.docgen import path_utils
from scripts.docgen.changed_context import classify_module
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

    def test_repo_escape_link_fails(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "repo_escape_link.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("escapes repo root", r.stdout + r.stderr)

    def test_absolute_backtick_path_fails(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "absolute_backtick_path.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_absolute_link_path_fails(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "absolute_link_path.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_windows_absolute_backtick_path_fails(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "windows_absolute_backtick_path.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_windows_absolute_link_path_fails(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "windows_absolute_link_path.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_absolute_backtick_path_with_spaces_fails(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "absolute_backtick_path_with_spaces.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_external_uri_links_are_ignored(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "external_uri_links.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")

    def test_file_uri_link_fails(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "file_uri_link.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_relative_backtick_path_with_spaces_passes(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "relative_backtick_path_with_spaces.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")

    def test_missing_relative_backtick_path_with_spaces_fails(self):
        r = run_script(
            "verify_paths.py",
            os.path.join(FIXTURES, "bad_backtick_path_with_spaces.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("space dir/missing.md", r.stdout + r.stderr)

    def test_ignore_repo_prefix_skips_generated_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            doc_path = os.path.join(tmp, "README.md")
            with open(doc_path, "w", encoding="utf-8") as f:
                f.write(
                    "# Temp Doc\n\n"
                    "Generated report: `docs/generated/missing.json`\n\n"
                    "[Evidence](docs/generated/evidence.json)\n"
                )

            failing = run_script("verify_paths.py", doc_path, "--root", tmp)
            self.assertNotEqual(failing.returncode, 0)
            self.assertIn(
                "docs/generated/missing.json", failing.stdout + failing.stderr
            )

            passing = run_script(
                "verify_paths.py",
                doc_path,
                "--root",
                tmp,
                "--ignore-repo-prefix",
                "docs/generated/",
            )
            self.assertEqual(
                passing.returncode,
                0,
                f"stdout: {passing.stdout}\nstderr: {passing.stderr}",
            )


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

    def test_duplicate_heading_anchors_pass(self):
        r = run_script(
            "verify_links.py",
            os.path.join(FIXTURES, "good_duplicate_anchors.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")

    def test_missing_duplicate_heading_anchor_fails(self):
        r = run_script(
            "verify_links.py",
            os.path.join(FIXTURES, "bad_duplicate_anchors.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("#repeat-2", r.stdout + r.stderr)

    def test_external_uri_links_are_ignored(self):
        r = run_script(
            "verify_links.py",
            os.path.join(FIXTURES, "external_uri_links.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")

    def test_absolute_link_path_fails(self):
        r = run_script(
            "verify_links.py",
            os.path.join(FIXTURES, "absolute_link_path.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_file_uri_link_fails(self):
        r = run_script(
            "verify_links.py",
            os.path.join(FIXTURES, "file_uri_link.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_repo_escape_link_fails(self):
        r = run_script(
            "verify_links.py",
            os.path.join(FIXTURES, "repo_escape_link.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("escapes repo root", r.stdout + r.stderr)


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


class TestVerifyDiagramSpecs(unittest.TestCase):
    def test_doc_without_mermaid_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            doc = os.path.join(tmp, "book", "src", "plain.md")
            os.makedirs(os.path.dirname(doc), exist_ok=True)
            with open(doc, "w", encoding="utf-8") as f:
                f.write("# Plain\n\nNo diagrams.\n")
            r = run_script("verify_diagram_specs.py", doc, "--root", tmp)
            self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")

    def test_missing_diagram_spec_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            doc = os.path.join(tmp, "book", "src", "stream.md")
            os.makedirs(os.path.dirname(doc), exist_ok=True)
            with open(doc, "w", encoding="utf-8") as f:
                f.write("# Stream\n\n```mermaid\nflowchart LR\n    A --> B\n```\n")
            r = run_script("verify_diagram_specs.py", doc, "--root", tmp)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(
                "missing diagram spec artifact", (r.stdout + r.stderr).lower()
            )

    def test_matching_diagram_spec_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            doc = os.path.join(tmp, "book", "src", "stream.md")
            os.makedirs(os.path.dirname(doc), exist_ok=True)
            with open(doc, "w", encoding="utf-8") as f:
                f.write("# Stream\n\n```mermaid\nflowchart LR\n    A --> B\n```\n")
            spec = os.path.join(
                tmp,
                "docs",
                "generated",
                "diagram_specs",
                "book__src__stream",
                "block-01.md",
            )
            os.makedirs(os.path.dirname(spec), exist_ok=True)
            with open(spec, "w", encoding="utf-8") as f:
                f.write("# Diagram Spec\n")
            r = run_script("verify_diagram_specs.py", doc, "--root", tmp)
            self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")

    def test_console_transcript_passes(self):
        r = run_script(
            "verify_commands.py",
            os.path.join(FIXTURES, "good_console_commands.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")

    def test_bad_console_commands_fail(self):
        r = run_script(
            "verify_commands.py",
            os.path.join(FIXTURES, "bad_console_commands.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")

    def test_console_heredoc_transcript_passes(self):
        r = run_script(
            "verify_commands.py",
            os.path.join(FIXTURES, "good_console_heredoc_commands.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")


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

    def test_extract_filenames_keeps_repo_paths_with_spaces(self):
        filenames = extract_filenames(
            "See `tests/fixtures/docgen/space dir/existing.md`."
        )
        self.assertEqual(filenames, ["tests/fixtures/docgen/space dir/existing.md"])

    def test_extract_filenames_ignores_non_path_backticks_with_spaces(self):
        filenames = extract_filenames("Route example: `GET /v1/health`.")
        self.assertEqual(filenames, [])

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

    def test_repo_escape_link_fails(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "repo_escape_link.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertIn("escapes repo root", r.stdout + r.stderr)

    def test_bad_markdown_link_reference_fails(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "bad_consistency_links.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertIn("fake_link_target.py", r.stdout + r.stderr)

    def test_absolute_backtick_path_fails(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "absolute_backtick_path.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_absolute_link_path_fails(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "absolute_link_path.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_windows_absolute_backtick_path_fails(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "windows_absolute_backtick_path.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_windows_absolute_link_path_fails(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "windows_absolute_link_path.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_absolute_backtick_path_with_spaces_fails(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "absolute_backtick_path_with_spaces.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)

    def test_external_uri_links_are_ignored(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "external_uri_links.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 0, f"stdout: {r.stdout}\nstderr: {r.stderr}")

    def test_file_uri_link_fails(self):
        r = run_script(
            "verify_doc_consistency.py",
            os.path.join(FIXTURES, "file_uri_link.md"),
            "--root",
            REPO_ROOT,
        )
        self.assertEqual(r.returncode, 1, f"stdout: {r.stdout}\nstderr: {r.stderr}")
        self.assertIn("absolute path forbidden", r.stdout + r.stderr)


class TestPathUtils(unittest.TestCase):
    def test_non_file_uri_schemes_are_external(self):
        self.assertTrue(path_utils.is_external_uri("mailto:docs@example.com"))
        self.assertTrue(
            path_utils.is_external_uri("ftp://example.com/releases/latest.txt")
        )

    def test_file_references_with_colons_are_not_treated_as_uris(self):
        self.assertFalse(path_utils.is_external_uri("build.sh:42"))

    def test_windows_paths_and_file_uris_are_not_external(self):
        self.assertFalse(path_utils.is_external_uri("C:/Program Files/tool/bin.exe"))
        self.assertFalse(path_utils.is_external_uri("file:///tmp/readme.md"))

    def test_file_uris_are_absolute_references(self):
        self.assertTrue(path_utils.is_absolute_reference("file:///tmp/readme.md"))


class TestChangedContext(unittest.TestCase):
    def test_classify_module_handles_git_style_paths(self):
        self.assertEqual(classify_module("src/main.cpp"), "src")
        self.assertEqual(classify_module("scripts/docgen/setup.sh"), "scripts")

    def test_classify_module_handles_windows_separators(self):
        self.assertEqual(classify_module(r"src\main.cpp"), "src")


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
