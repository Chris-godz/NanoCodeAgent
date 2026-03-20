#!/usr/bin/env python3
"""Structure checks for the documentation collaboration style module.

Verifies that the style spec, overview-writer skill, and clarity-reviewer skill
exist and are referenced from AGENTS.md and the docgen task prompts.
"""

import os
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DOCS = os.path.join(REPO_ROOT, "docs")
AGENTS = os.path.join(REPO_ROOT, "AGENTS.md")
SKILLS = os.path.join(REPO_ROOT, ".agents", "skills")
TASKS = os.path.join(REPO_ROOT, "scripts", "docgen", "tasks")

STYLE_SPEC = os.path.join(DOCS, "documentation-collaboration-style.md")
OVERVIEW_WRITER = os.path.join(SKILLS, "docgen-overview-writer", "SKILL.md")
CLARITY_REVIEWER = os.path.join(SKILLS, "doc-clarity-reviewer", "SKILL.md")
TASK_WRITE = os.path.join(TASKS, "ai_doc_restructure_or_update.md")
TASK_REVIEW = os.path.join(TASKS, "ai_review.md")


class TestStyleModuleFilesExist(unittest.TestCase):
    """Required style module files must exist."""

    def test_style_spec_exists(self):
        self.assertTrue(os.path.isfile(STYLE_SPEC), f"Missing: {STYLE_SPEC}")

    def test_overview_writer_skill_exists(self):
        self.assertTrue(os.path.isfile(OVERVIEW_WRITER), f"Missing: {OVERVIEW_WRITER}")

    def test_clarity_reviewer_skill_exists(self):
        self.assertTrue(os.path.isfile(CLARITY_REVIEWER), f"Missing: {CLARITY_REVIEWER}")


class TestAGENTSReferencesStyleModule(unittest.TestCase):
    """AGENTS.md must reference the style spec and the two skills."""

    def setUp(self):
        with open(AGENTS, encoding="utf-8") as f:
            self.content = f.read()

    def test_agents_references_documentation_collaboration_style(self):
        self.assertIn(
            "documentation-collaboration-style",
            self.content,
            "AGENTS.md should reference docs/documentation-collaboration-style.md",
        )

    def test_agents_mentions_overview_writer_or_clarity_reviewer(self):
        self.assertTrue(
            "docgen-overview-writer" in self.content or "overview-writer" in self.content,
            "AGENTS.md should mention overview-writer skill",
        )
        self.assertTrue(
            "doc-clarity-reviewer" in self.content or "clarity-reviewer" in self.content,
            "AGENTS.md should mention clarity-reviewer skill",
        )


class TestTaskWriteReferencesStyleAndWriter(unittest.TestCase):
    """ai_doc_restructure_or_update.md must reference style spec and overview-writer in Step 0."""

    def setUp(self):
        with open(TASK_WRITE, encoding="utf-8") as f:
            self.content = f.read()

    def test_task_references_documentation_collaboration_style(self):
        self.assertIn(
            "documentation-collaboration-style",
            self.content,
            "ai_doc_restructure_or_update.md Step 0 should reference the style spec",
        )

    def test_task_references_overview_writer(self):
        self.assertIn(
            "docgen-overview-writer",
            self.content,
            "ai_doc_restructure_or_update.md Step 0 should reference overview-writer skill",
        )


class TestTaskReviewReferencesStyleAndClarityReviewer(unittest.TestCase):
    """ai_review.md must reference style spec and doc-clarity-reviewer in Step 0."""

    def setUp(self):
        with open(TASK_REVIEW, encoding="utf-8") as f:
            self.content = f.read()

    def test_task_references_documentation_collaboration_style(self):
        self.assertIn(
            "documentation-collaboration-style",
            self.content,
            "ai_review.md Step 0 should reference the style spec",
        )

    def test_task_references_clarity_reviewer(self):
        self.assertIn(
            "doc-clarity-reviewer",
            self.content,
            "ai_review.md Step 0 should reference doc-clarity-reviewer skill",
        )

    def test_task_output_requires_clarity_section(self):
        self.assertIn(
            "Clarity",
            self.content,
            "ai_review.md output should require a Clarity / 理解效果 section",
        )

    def test_task_review_mentions_diagram_rubric_questions(self):
        for phrase in (
            "one main question",
            "detail downshift",
            "short scannable titles",
            "noisy center",
            "caption responsibility",
        ):
            self.assertIn(phrase, self.content, f"ai_review.md should require diagram rubric phrase: {phrase}")

    def test_task_review_requires_diagram_evidence_paths(self):
        for phrase in (
            "Diagram evidence used",
            "artifact report path",
            "screenshot path",
        ):
            self.assertIn(phrase, self.content, f"ai_review.md should require evidence phrase: {phrase}")

    def test_task_review_treats_summary_as_boundary_support(self):
        self.assertIn(
            "boundary-support file",
            self.content,
            "ai_review.md should explain SUMMARY boundary-support handling",
        )


if __name__ == "__main__":
    unittest.main()
