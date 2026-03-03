#!/usr/bin/env bash
# generate.sh — wrapper script for the AI documentation generator.
#
# Usage:
#   GITHUB_TOKEN=<token> CODE_DIFF="<diff>" bash tools/docgen/generate.sh
#
# Environment variables:
#   GITHUB_TOKEN   - GitHub token with models:read permission (required)
#   DOCGEN_MODEL   - Model to use (default: openai/gpt-4.1-mini)
#   CODE_DIFF      - The git diff to analyze (required)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[generate.sh] Running AI documentation generator..."
python3 "${SCRIPT_DIR}/docgen.py"
echo "[generate.sh] Done."
