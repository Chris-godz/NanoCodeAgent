#!/usr/bin/env bash
# scripts/docgen/setup.sh — Bootstrap the docgen toolchain environment.
#
# Purpose:
#   Verify that required tools (Python 3, git, npm, Python venv support) are
#   available for the docgen scripts. Intended to be run once before first use
#   or after a clean checkout.
#
# Usage:
#   ./scripts/docgen/setup.sh
#
# Exit codes:
#   0 — environment is ready
#   1 — a required tool is missing
#
# Notes:
#   This script does NOT install Python packages or system packages. Mermaid
#   render verification bootstraps `.venv/`, Playwright, Chromium, and the
#   local Mermaid bundle on first use; this script only checks that the host
#   can support that bootstrap.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=== docgen setup ==="
echo "Repo root: $REPO_ROOT"

# Check Python 3
if ! command -v python3 &>/dev/null; then
    echo "ERROR: python3 is required but not found in PATH." >&2
    exit 1
fi
echo "Python 3: $(python3 --version)"

# Check git
if ! command -v git &>/dev/null; then
    echo "ERROR: git is required but not found in PATH." >&2
    exit 1
fi
echo "Git: $(git --version)"

# Check npm / node for Mermaid bootstrap
if ! command -v npm &>/dev/null; then
    echo "ERROR: npm is required for Mermaid render verification but not found in PATH." >&2
    exit 1
fi
echo "npm: $(npm --version)"

if command -v node &>/dev/null; then
    echo "Node: $(node --version)"
fi

# Check Python venv support for Playwright bootstrap
if ! python3 -m venv --help &>/dev/null; then
    echo "ERROR: python3 venv support is required for Mermaid render verification." >&2
    exit 1
fi
echo "python3 -m venv: available"

if python3 -m pytest --version &>/dev/null; then
    echo "pytest: available"
else
    echo "pytest: not installed (optional; bundled unittest regression tests still work)"
fi

# Verify we are in a git repo
if ! git -C "$REPO_ROOT" rev-parse --is-inside-work-tree &>/dev/null; then
    echo "ERROR: $REPO_ROOT is not inside a git repository." >&2
    exit 1
fi

echo "=== docgen environment ready ==="
echo "Note: first Mermaid verify bootstraps .venv/, Playwright, Chromium, and tmp/mermaid-tools/."
