#!/usr/bin/env bash
# scripts/docgen/run_docgen_verify.sh
#
# Independent codex exec verification for docgen E2E:
# 1) Read-only flow understanding
# 2) Black-box run interpretation (run E2E first, then codex reads artifacts)
# 3) Fixture classification / review
# Requires Codex CLI; no fallback.
#
# Usage:
#   ./scripts/docgen/run_docgen_verify.sh
# For step 2, run full E2E first: bash scripts/docgen/run_docgen_e2e.sh
#
# Exit codes:
#   0 — all verification steps that were run succeeded
#   1 — codex missing or a verification step failed

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

GENERATED="docs/generated"
UNDERSTANDING_OUT="$GENERATED/docgen_flow_understanding.txt"
FIXTURE_VERIFICATION_OUT="$GENERATED/docgen_fixture_verification.txt"
CASE_B_FILES="tests/fixtures/docgen/change_impact/case_b_changed_files.txt"
MINIMAL_IMPACT="tests/fixtures/docgen/e2e/minimal_impact_report.md"

log() { echo ">>> [$(date +%H:%M:%S)] $*"; }
fail() { echo "!!! FAIL: $*" >&2; exit 1; }

# ── Require codex ──────────────────────────────────────────────────────
if ! command -v codex >/dev/null 2>&1; then
    echo "codex exec is required for docgen verification. Install the Codex CLI or run in a Codex-enabled environment." >&2
    exit 1
fi

mkdir -p "$GENERATED"

# ── 1) Read-only flow understanding ─────────────────────────────────────
log "Verification 1: Read-only flow understanding"
codex exec "Read the Documentation Automation Rules section from AGENTS.md (between '## Documentation Automation Rules' and '## AI Customization Layout'), .agents/skills/docgen-change-impact/SKILL.md, scripts/docgen/run_docgen_e2e.sh, and the contents of docs/generated/*.md if present. Summarize the docgen E2E flow and the structure of generated artifacts. Do not modify any files." 2>&1 | tee "$UNDERSTANDING_OUT"
log "Verification 1: OK — output in $UNDERSTANDING_OUT"

# ── 2) Black-box execution interpretation ────────────────────────────────
log "Verification 2: Black-box execution interpretation"
if [ ! -f "$GENERATED/docgen_e2e_summary.md" ] || [ ! -f "$GENERATED/change_impact_report.md" ]; then
    log "Verification 2: SKIP — run 'bash scripts/docgen/run_docgen_e2e.sh' first to produce docgen_e2e_summary.md and change_impact_report.md"
else
    codex exec "Read docs/generated/docgen_e2e_summary.md, docs/generated/change_impact_report.md, and list docs/generated/candidates/. Summarize what steps were executed, what candidate drafts were produced, and what the final recommendation is. Do not modify any files."
    log "Verification 2: OK"
fi

# ── 3) Fixture classification / review ──────────────────────────────────
log "Verification 3: Fixture classification"
if [ ! -f "$CASE_B_FILES" ]; then
    log "Verification 3: SKIP — fixture $CASE_B_FILES not found"
else
    codex exec "Read $CASE_B_FILES and optionally $MINIMAL_IMPACT if present. Based only on the changed file list and repo context, should README.md and src/main.cpp changes be classified as requiring doc updates (required), optional, or no update needed? Output your classification and brief rationale. Do not modify any files." 2>&1 | tee "$FIXTURE_VERIFICATION_OUT"
    log "Verification 3: OK — output in $FIXTURE_VERIFICATION_OUT"
fi

log "Docgen verification completed."
