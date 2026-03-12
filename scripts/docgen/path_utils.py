#!/usr/bin/env python3
"""Shared helpers for docgen path detection."""

import os
import re


ROOT_FILE_NAMES = {"CMakeLists.txt", "Dockerfile", "LICENSE", "Makefile"}
ROOT_FILE_SUFFIXES = {
    ".cfg",
    ".cmake",
    ".conf",
    ".ini",
    ".json",
    ".md",
    ".rst",
    ".sh",
    ".toml",
    ".txt",
    ".xml",
    ".yaml",
    ".yml",
}
ROOT_DOTFILES = {
    ".clang-format",
    ".clang-tidy",
    ".editorconfig",
    ".env",
    ".env.example",
    ".gitattributes",
    ".gitignore",
}


def is_root_file_candidate(candidate: str) -> bool:
    """Return whether a bare token looks like a root-level repo file."""
    if candidate in ROOT_FILE_NAMES or candidate in ROOT_DOTFILES:
        return True

    stem, suffix = os.path.splitext(candidate)
    return bool(stem) and suffix in ROOT_FILE_SUFFIXES


def is_repo_reference(candidate: str) -> bool:
    """Return whether a backtick token should be checked against the repo."""
    if candidate.startswith("http") or candidate.startswith("-") or " " in candidate:
        return False

    return "/" in candidate or is_root_file_candidate(candidate)


def extract_markdown_link_targets(content: str) -> list[str]:
    """Extract local Markdown link targets without anchors."""
    targets: set[str] = set()

    for match in re.finditer(r"\]\(([^)]+)\)", content):
        target = match.group(1).strip()
        if not target.startswith("http") and not target.startswith("#"):
            target = target.split("#", 1)[0]
            if target:
                targets.add(target)

    return sorted(targets)


def resolve_doc_relative_target(
    repo_root: str, doc_file: str, target: str
) -> tuple[str, str, bool]:
    """Resolve a document-relative target and report whether it stays in-repo."""
    repo_root = os.path.abspath(repo_root)
    doc_dir = os.path.dirname(os.path.abspath(doc_file))
    full_path = os.path.abspath(os.path.join(doc_dir, target))

    try:
        in_repo = os.path.commonpath((repo_root, full_path)) == repo_root
    except ValueError:
        in_repo = False

    reference = os.path.relpath(full_path, repo_root) if in_repo else target
    return reference, full_path, in_repo
