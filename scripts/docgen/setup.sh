#!/usr/bin/env bash
# scripts/docgen/setup.sh — Bootstrap the docgen toolchain environment.
#
# Purpose:
#   Verify that required tools (Python 3, git) are available and install any
#   Python dependencies needed by the docgen scripts. Intended to be run once
#   before first use or after a clean checkout.
#
# Usage:
#   ./scripts/docgen/setup.sh
#
# Exit codes:
#   0 — environment is ready
#   1 — a required tool is missing
#
# Notes:
#   This script does NOT install system packages. It checks availability and
#   exits with a clear error message if something is missing.

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

# Verify we are in a git repo
if ! git -C "$REPO_ROOT" rev-parse --is-inside-work-tree &>/dev/null; then
    echo "ERROR: $REPO_ROOT is not inside a git repository." >&2
    exit 1
fi

echo "=== docgen environment ready ==="
