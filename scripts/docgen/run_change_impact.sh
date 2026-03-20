#!/usr/bin/env bash
# scripts/docgen/run_change_impact.sh
#
# Unified entry point for the change-impact docgen workflow.
# Fixed pipeline: read rules → read skill → run changed_context → generate or verify report → verify paths/links.
# All steps above are blocking; any failure exits with 1.
#
# Usage:
#   ./scripts/docgen/run_change_impact.sh
#   REF=main ./scripts/docgen/run_change_impact.sh
#
# Exit codes:
#   0 — pipeline and verification succeeded
#   1 — a blocking step failed

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

GENERATED="docs/generated"
CONTEXT_OUTPUT="$GENERATED/change_context_output.md"
REPORT="$GENERATED/change_impact_report.md"
mkdir -p "$GENERATED"

log() { echo ">>> [$(date +%H:%M:%S)] $*"; }
fail() { echo "!!! FAIL: $*" >&2; exit 1; }

# ── Step 1: Read AGENTS.md Documentation Automation Rules (blocking) ───
log "Step 1: Reading Documentation Automation Rules from AGENTS.md"
RULES=$(sed -n '/## Documentation Automation Rules/,/## AI Customization Layout/p' AGENTS.md)
if [ -z "$RULES" ]; then
    fail "Heading '## Documentation Automation Rules' not found in AGENTS.md. Cannot proceed."
fi
echo "$RULES" | head -5
log "Step 1: OK — rules section found ($(echo "$RULES" | wc -l) lines)"

# ── Step 2: Read skill definition (blocking) ───────────────────────────
SKILL_FILE=".agents/skills/docgen-change-impact/SKILL.md"
log "Step 2: Reading skill definition from $SKILL_FILE"
if [ ! -f "$SKILL_FILE" ]; then
    fail "$SKILL_FILE not found."
fi
head -5 "$SKILL_FILE"
log "Step 2: OK — skill file exists"

# ── Step 3: Run changed_context.py (blocking) ───────────────────────────
log "Step 3: Running changed_context.py"
python3 scripts/docgen/changed_context.py --root . --ref "${REF:-HEAD~1}" --output "$CONTEXT_OUTPUT"
log "Step 3: OK — wrote $CONTEXT_OUTPUT"

# ── Step 4: Generate or verify change_impact_report.md (blocking) ────────
log "Step 4: Generate or verify $REPORT"
if [ ! -f "$REPORT" ]; then
    log "  Generating report from context output"
    python3 scripts/docgen/generate_impact_report.py --root . --context "$CONTEXT_OUTPUT" --output "$REPORT"
    log "Step 4: OK — generated $REPORT"
else
    log "  Report exists — skipping generation"
    log "Step 4: OK — report present"
fi

# ── Step 5: Verification on report (blocking) ───────────────────────────
log "Step 5: Running verification on $REPORT"
log "  5a: verify_paths.py (blocking)"
if ! python3 scripts/docgen/verify_paths.py "$REPORT" --root .; then
    fail "verify_paths.py failed on $REPORT"
fi
log "  5b: verify_links.py (blocking)"
if ! python3 scripts/docgen/verify_links.py "$REPORT" --root .; then
    fail "verify_links.py failed on $REPORT"
fi
log "Step 5: OK — verification passed"

# ── Result ──────────────────────────────────────────────────────────────
echo ""
log "Change impact pipeline completed successfully."
