#!/usr/bin/env bash
# scripts/docgen/run_docgen_e2e.sh
#
# Docgen E2E: fixed target docs (README + book only). Pipeline:
#   change_facts → doc_scope_decision (Agent) → reference_context → doc_restructure_or_update (Agent) → verify → reviewer (Agent) → E2E summary.
# Requires Codex CLI for Agent stages.
#
# Usage:
#   ./scripts/docgen/run_docgen_e2e.sh
#   REF=main ./scripts/docgen/run_docgen_e2e.sh
#
# Exit codes:
#   0 — pipeline succeeded
#   1 — blocking step failed (including missing codex)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

GENERATED="docs/generated"
CHANGE_FACTS="$GENERATED/change_facts.json"
SCOPE_DECISION="$GENERATED/doc_scope_decision.json"
REFERENCE_CONTEXT="$GENERATED/reference_context.json"
MISSING_DIAGRAM_SPECS="$GENERATED/missing_diagram_specs.json"
VERIFY_REPORT="$GENERATED/verify_report.json"
VALIDATION_REPORT="$GENERATED/docgen_validation_report.md"
REVIEW_REPORT="$GENERATED/docgen_review_report.md"
E2E_SUMMARY="$GENERATED/docgen_e2e_summary.md"
TASKS_DIR="$SCRIPT_DIR/tasks"
mkdir -p "$GENERATED"

log() { echo ">>> [$(date +%H:%M:%S)] $*"; }
fail() { echo "!!! FAIL: $*" >&2; exit 1; }

# ── Step 1: Read AGENTS.md Documentation Automation Rules (blocking) ───
log "Step 1: Reading Documentation Automation Rules from AGENTS.md"
RULES=$(sed -n '/## Documentation Automation Rules/,/## AI Customization Layout/p' AGENTS.md)
if [ -z "$RULES" ]; then
    fail "Heading '## Documentation Automation Rules' not found in AGENTS.md. Cannot proceed."
fi
echo "$RULES" | head -3
log "Step 1: OK — rules section found ($(echo "$RULES" | wc -l) lines)"

# ── Step 2: Check skill files exist (blocking) ──────────────────────────
log "Step 2: Checking required skill files"
for sk in docgen-change-impact docgen-tutorial-update docgen-fact-check docgen-reviewer; do
    if [ ! -f ".agents/skills/$sk/SKILL.md" ]; then
        fail ".agents/skills/$sk/SKILL.md not found."
    fi
done
log "Step 2: OK — required skills present"

# ── Step 2b: Require codex (blocking) ───────────────────────────────────
log "Step 2b: Checking Codex CLI (required for AI stages)"
if ! command -v codex >/dev/null 2>&1; then
    echo "codex exec is required for AI stages (doc scope decision, doc update, review, summary)." >&2
    echo "Install the Codex CLI or run this script in a Codex-enabled environment." >&2
    exit 1
fi
log "Step 2b: OK — codex found"

# ── Step 3: Deterministic — change_facts (JSON only) ────────────────────
log "Step 3: Change facts (change_facts.py)"
python3 scripts/docgen/change_facts.py --root . --ref "${REF:-HEAD~1}" --output "$CHANGE_FACTS"
log "Step 3: OK — $CHANGE_FACTS written"

# ── Step 4: AI doc scope decision (codex exec) ─────────────────────────
log "Step 4: AI doc scope decision (codex exec)"
if [ ! -f "$TASKS_DIR/ai_doc_scope_decision.md" ]; then
    fail "Task file $TASKS_DIR/ai_doc_scope_decision.md not found."
fi
codex exec --full-auto "$(cat "$TASKS_DIR/ai_doc_scope_decision.md")"
log "Step 4: OK — $SCOPE_DECISION expected"

# ── Step 5: Deterministic — reference_context ─────────────────────────
log "Step 5: Reference context (reference_context.py)"
python3 scripts/docgen/reference_context.py --root . --scope "$SCOPE_DECISION" --facts "$CHANGE_FACTS" --output "$REFERENCE_CONTEXT"
log "Step 5: OK — $REFERENCE_CONTEXT written"

# ── Step 6: AI doc restructure or update (codex exec) ───────────────────
log "Step 6: AI doc restructure or update (README + book)"
if [ ! -f "$TASKS_DIR/ai_doc_restructure_or_update.md" ]; then
    fail "Task file $TASKS_DIR/ai_doc_restructure_or_update.md not found."
fi
codex exec --full-auto "$(cat "$TASKS_DIR/ai_doc_restructure_or_update.md")"
log "Step 6: OK — README and book updated in place"

# ── Step 6b: Focused diagram spec backfill when needed ──────────────────
log "Step 6b: Checking for missing diagram specs"
if python3 scripts/docgen/missing_diagram_specs.py --root . --scope "$SCOPE_DECISION" --output "$MISSING_DIAGRAM_SPECS" >/dev/null 2>&1; then
    log "Step 6b: OK — no missing diagram specs"
else
    log "Step 6b: Missing diagram specs detected; running focused backfill"
    if [ ! -f "$TASKS_DIR/ai_diagram_spec_backfill.md" ]; then
        fail "Task file $TASKS_DIR/ai_diagram_spec_backfill.md not found."
    fi
    codex exec --full-auto "$(cat "$TASKS_DIR/ai_diagram_spec_backfill.md")"
    python3 scripts/docgen/missing_diagram_specs.py --root . --scope "$SCOPE_DECISION" --output "$MISSING_DIAGRAM_SPECS" >/dev/null \
        || fail "diagram spec backfill did not satisfy missing spec requirements"
    log "Step 6b: OK — missing diagram specs backfilled"
fi

# ── Step 7: Verify target docs → verify_report.json ────────────────────
log "Step 7: Verify (run_verify_report.py)"
if ! python3 scripts/docgen/run_verify_report.py --root . --scope "$SCOPE_DECISION" --report-json "$VERIFY_REPORT" --report-md "$VALIDATION_REPORT"; then
    fail "run_verify_report.py failed (blocking checks)"
fi
log "Step 7: OK — $VERIFY_REPORT written"

# ── Step 8: AI review (codex exec) ──────────────────────────────────────
log "Step 8: AI review (codex exec)"
if [ ! -f "$TASKS_DIR/ai_review.md" ]; then
    fail "Task file $TASKS_DIR/ai_review.md not found."
fi
codex exec --full-auto "$(cat "$TASKS_DIR/ai_review.md")"
log "Step 8: OK — $REVIEW_REPORT expected"

# ── Step 9: Deterministic E2E summary (render from JSON evidence) ───────
log "Step 9: Render docgen_e2e_summary.md (evidence-driven, no Codex)"
python3 scripts/docgen/render_docgen_e2e_summary.py --root .
log "Step 9: OK — $E2E_SUMMARY written"

# ── Result ──────────────────────────────────────────────────────────────
echo ""
log "Docgen E2E pipeline completed successfully. Open $E2E_SUMMARY for human review."
