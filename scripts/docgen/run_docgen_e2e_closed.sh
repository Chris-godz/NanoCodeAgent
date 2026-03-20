#!/usr/bin/env bash
# scripts/docgen/run_docgen_e2e_closed.sh
#
# Docgen E2E with closed loop: Phase 1 (Steps 1–6) then Phase 2 (verify → repair / review → rework) via run_docgen_e2e_loop.py.
# Same prerequisites as run_docgen_e2e.sh (Codex CLI). Exit 0 on summary written; exit 1 on TerminalFail or Phase 1 failure.
#
# Usage:
#   ./scripts/docgen/run_docgen_e2e_closed.sh
#   REF=main ./scripts/docgen/run_docgen_e2e_closed.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

GENERATED="docs/generated"
CHANGE_FACTS="$GENERATED/change_facts.json"
SCOPE_DECISION="$GENERATED/doc_scope_decision.json"
REFERENCE_CONTEXT="$GENERATED/reference_context.json"
MISSING_DIAGRAM_SPECS="$GENERATED/missing_diagram_specs.json"
TASKS_DIR="$SCRIPT_DIR/tasks"
mkdir -p "$GENERATED"

log() { echo ">>> [$(date +%H:%M:%S)] $*"; }
fail() { echo "!!! FAIL: $*" >&2; exit 1; }

# ── Step 0: Clean up stale artifacts from previous runs ─────────────────────
log "Step 0: Cleaning up stale artifacts from previous runs"
rm -f \
    "$GENERATED"/change_facts.json \
    "$GENERATED"/doc_scope_decision.json \
    "$GENERATED"/reference_context.json \
    "$GENERATED"/missing_diagram_specs.json \
    "$GENERATED"/verify_report.json \
    "$GENERATED"/docgen_validation_report.md \
    "$GENERATED"/docgen_review_report.md \
    "$GENERATED"/docgen_review_report.json \
    "$GENERATED"/review_rework_report.json \
    "$GENERATED"/verify_repair_report.json \
    "$GENERATED"/e2e_loop_state.json \
    "$GENERATED"/e2e_run_evidence.json \
    "$GENERATED"/e2e_failure_report.md \
    "$GENERATED"/docgen_e2e_summary.md
rm -rf "$GENERATED"/diagram_specs "$GENERATED"/diagram_artifacts
log "Step 0: OK"

# ── Step 1: Documentation Automation Rules ───────────────────────────────
log "Step 1: Reading Documentation Automation Rules from AGENTS.md"
RULES=$(sed -n '/## Documentation Automation Rules/,/## AI Customization Layout/p' AGENTS.md)
if [ -z "$RULES" ]; then
    fail "Heading '## Documentation Automation Rules' not found in AGENTS.md."
fi
log "Step 1: OK"

# ── Step 2: Skills and codex ─────────────────────────────────────────────
log "Step 2: Checking required skill files"
for sk in docgen-change-impact docgen-tutorial-update docgen-fact-check docgen-reviewer; do
    if [ ! -f ".agents/skills/$sk/SKILL.md" ]; then
        fail ".agents/skills/$sk/SKILL.md not found."
    fi
done
log "Step 2b: Checking Codex CLI"
if ! command -v codex >/dev/null 2>&1; then
    echo "codex exec is required for AI stages." >&2
    exit 1
fi
log "Step 2: OK"

# ── Step 3: change_facts ───────────────────────────────────────────────
log "Step 3: Change facts"
if [ -n "${DOCGEN_FILES:-}" ]; then
    log "Step 3: Using explicit files list from DOCGEN_FILES=$DOCGEN_FILES"
    python3 scripts/docgen/change_facts.py --root . --files "$DOCGEN_FILES" --output "$CHANGE_FACTS"
else
    python3 scripts/docgen/change_facts.py --root . --ref "${REF:-HEAD~1}" --output "$CHANGE_FACTS"
fi
log "Step 3: OK — $CHANGE_FACTS"

# ── Step 4: AI doc scope decision ───────────────────────────────────────
log "Step 4: AI doc scope decision (codex exec)"
[ -f "$TASKS_DIR/ai_doc_scope_decision.md" ] || fail "Task file ai_doc_scope_decision.md not found."
codex exec --full-auto "$(cat "$TASKS_DIR/ai_doc_scope_decision.md")"
log "Step 4: OK — $SCOPE_DECISION"

# ── Step 5: reference_context ───────────────────────────────────────────
log "Step 5: Reference context"
python3 scripts/docgen/reference_context.py --root . --scope "$SCOPE_DECISION" --facts "$CHANGE_FACTS" --output "$REFERENCE_CONTEXT"
log "Step 5: OK — $REFERENCE_CONTEXT"

# ── Step 6: AI doc restructure or update ────────────────────────────────
log "Step 6: AI doc restructure or update (README + book)"
[ -f "$TASKS_DIR/ai_doc_restructure_or_update.md" ] || fail "Task file ai_doc_restructure_or_update.md not found."
codex exec --full-auto "$(cat "$TASKS_DIR/ai_doc_restructure_or_update.md")"
log "Step 6: OK — draft ready"

# ── Step 6b: Focused diagram spec backfill when needed ──────────────────
log "Step 6b: Checking for missing diagram specs"
if python3 scripts/docgen/missing_diagram_specs.py --root . --scope "$SCOPE_DECISION" --output "$MISSING_DIAGRAM_SPECS" >/dev/null 2>&1; then
    log "Step 6b: OK — no missing diagram specs"
else
    log "Step 6b: Missing diagram specs detected; running focused backfill"
    [ -f "$TASKS_DIR/ai_diagram_spec_backfill.md" ] || fail "Task file ai_diagram_spec_backfill.md not found."
    codex exec --full-auto "$(cat "$TASKS_DIR/ai_diagram_spec_backfill.md")"
    python3 scripts/docgen/missing_diagram_specs.py --root . --scope "$SCOPE_DECISION" --output "$MISSING_DIAGRAM_SPECS" >/dev/null \
        || fail "diagram spec backfill did not satisfy missing spec requirements"
    log "Step 6b: OK — missing diagram specs backfilled"
fi

# ── Phase 2: Closed loop (verify → repair / review → rework → summary) ─
log "Phase 2: Closed loop (run_docgen_e2e_loop.py)"
python3 scripts/docgen/run_docgen_e2e_loop.py --root .
r=$?
if [ $r -eq 0 ]; then
    log "Docgen E2E closed loop completed. Open $GENERATED/docgen_e2e_summary.md for human review."
else
    log "Closed loop ended with TerminalFail. See $GENERATED/e2e_failure_report.md and last verify/review reports."
fi
exit $r
