# Docgen Scripts

Deterministic scripts that support the documentation automation workflow. These scripts handle scanning, extraction, and verification — tasks that should NOT be left to the model to guess.

## Script Inventory

| Script | Purpose | Status | Blocking? |
|--------|---------|--------|-----------|
| `scripts/docgen/setup.sh` | Verify environment prerequisites (Python 3, git, npm, Python venv bootstrap support) | Functional | Yes |
| `scripts/docgen/run_repo_understanding.sh` | Unified entry point for repo-understanding pipeline | Functional | Yes |
| `scripts/docgen/repo_map.py` | Generate repository structure map | Functional | Yes |
| `scripts/docgen/doc_inventory.py` | Index documentation files and assess staleness | Functional | Yes |
| `scripts/docgen/example_inventory.py` | Map examples to tutorial sections | Functional | Yes |
| `scripts/docgen/change_facts.py` | Extract change facts (JSON): changed files, change types, public surface signals | Functional | Yes |
| `scripts/docgen/changed_context.py` | Legacy: `--format json` writes change_facts.json; `--format md` deprecated (no adjacent/candidate) | Functional | — |
| `scripts/docgen/run_change_impact.sh` | Legacy change-impact pipeline (not used in E2E) | Functional | — |
| `scripts/docgen/reference_context.py` | Collect reference context (examples, tests, source, doc excerpts) after scope decision | Functional | Yes |
| `scripts/docgen/run_verify_report.py` | Verify target docs (README + book) and write verify_report.json | Functional | Yes |
| `scripts/docgen/verify_mermaid.py` | Render-check Mermaid fences, emit SVG/PNG review artifacts, and fail on parse/render errors | Functional | Yes |
| `scripts/docgen/generate_impact_report.py` | Legacy: build change_impact_report from context (not used in E2E) | Functional | — |
| `scripts/docgen/run_docgen_e2e.sh` | **E2E pipeline**: change_facts → doc_scope_decision → reference_context → doc update → verify → review → summary; target docs = README + book only | Functional | Yes |
| `scripts/docgen/run_docgen_e2e_closed.sh` | **E2E closed loop**: Phase 1 (Steps 1–6) then Phase 2 (verify → repair / review → rework) via `run_docgen_e2e_loop.py`; state in `e2e_loop_state.json`; failure report on TerminalFail | Functional | Yes |
| `scripts/docgen/run_docgen_e2e_loop.py` | Closed-loop orchestrator: verify → verify_repair (≤2) → review → review_rework (≤2) → `render_docgen_e2e_summary.py`; total repair actions ≤ 4 | Functional | Yes |
| `scripts/docgen/render_docgen_e2e_summary.py` | Deterministic `docgen_e2e_summary.md` from JSON evidence (closed + open E2E); no Codex | Functional | Yes |
| `scripts/docgen/run_docgen_verify.sh` | Independent codex exec verification (flow understanding, black-box, fixture classification) | Functional | Yes |
| `scripts/docgen/run_validation_report.py` | Legacy: aggregate verify_* on candidates (E2E uses run_verify_report on target docs) | Functional | — |
| `scripts/docgen/run_review_report.py` | Heuristic review (standalone / reference; not used in E2E main path) | Functional | No |
| `scripts/docgen/verify_paths.py` | Check that documented paths exist | Functional | Yes |
| `scripts/docgen/verify_links.py` | Validate internal Markdown links and anchors | Functional | Yes |
| `scripts/docgen/verify_doc_consistency.py` | Check path/script references against repo | Minimal | No |
| `scripts/docgen/verify_commands.py` | Validate shell command syntax via `bash -n` | Minimal | No |

### Status Legend

- **Functional** — complete implementation, exit codes reliable.
- **Minimal** — core checks implemented, some verification categories NOT_IMPLEMENTED.

### Blocking vs Non-Blocking

**Blocking** scripts must pass (exit 0) for the pipeline to continue. If a blocking script fails, the pipeline halts and the error must be fixed.

**Non-blocking** scripts report findings but do not gate the pipeline. Their output is still printed, but `scripts/docgen/run_repo_understanding.sh` does not fail the workflow on their exit codes.

## CI fixtures (GitHub Actions / PR)

PR jobs call **`run_verify_report.py`** with a committed scope file so the same verifier runs locally and in CI:

- `tests/fixtures/ci_doc_scope.json` — `approved_targets` for README + book chapters checked on every PR.
- `tests/fixtures/ci_diagram_specs/` — mirror layout of `docs/generated/diagram_specs/<slug>/block-NN.md` for Mermaid-bearing chapters (the real `docs/generated/` tree is gitignored). **Update this mirror** when you add or reorder Mermaid blocks in the book.

Full closed-loop docgen uses the same shell entry as local: `bash scripts/docgen/run_docgen_e2e_closed.sh` (see `.github/workflows/core-ci.yml`, job `full-doc-automation`, triggered via `workflow_dispatch`).

## Unified Entry Point

```bash
bash scripts/docgen/run_repo_understanding.sh
```

This runs the deterministic repo-understanding prep pipeline in order:
1. Read AGENTS.md documentation automation rules (targeted `sed` extract).
2. Verify skill file exists.
3. Run `scripts/docgen/repo_map.py`, `scripts/docgen/doc_inventory.py`, and `scripts/docgen/example_inventory.py`.
4. Check whether `docs/generated/repo_understanding_summary.md` already exists.
5. Run blocking verifiers (`verify_paths.py`, `verify_links.py`) if the summary exists.
6. Run non-blocking verifiers (`verify_doc_consistency.py`, `verify_commands.py`) if the summary exists.

The script does not synthesize `repo_understanding_summary.md` itself; it prepares the deterministic inputs and verifies an existing summary.

## Milestone 2: Change Impact Analysis

**Purpose**: Input a git diff (or a file list), output `docs/generated/change_impact_report.md` with documentation impact classified as **required**, **optional**, or **no update needed**. This pipeline does not modify real docs; it only produces a report for human or downstream skill use.

**Entry point**:

```bash
bash scripts/docgen/run_change_impact.sh
```

Optional: set `REF` to override the git diff base (default `HEAD~1`):

```bash
REF=main bash scripts/docgen/run_change_impact.sh
```

**Pipeline steps** (all blocking): read Documentation Automation Rules from AGENTS.md → read `.agents/skills/docgen-change-impact/SKILL.md` → run `changed_context.py` → generate or verify `change_impact_report.md` → run `verify_paths.py` and `verify_links.py` on the report.

**Change impact smoke test** (local reproduction):

```bash
bash scripts/docgen/setup.sh
bash scripts/docgen/run_change_impact.sh
# Then optionally re-verify the report:
python3 scripts/docgen/verify_paths.py docs/generated/change_impact_report.md --root .
python3 scripts/docgen/verify_links.py docs/generated/change_impact_report.md --root .
```

**Regression tests** (including change impact):

```bash
python3 -m unittest tests.test_docgen_verify tests.test_docgen_verify_consistency_links tests.test_docgen_change_impact
```

**Known limitations**:

- Classification is heuristic; it does not replace human judgment.
- The pipeline does not auto-update any documentation.
- Report generation depends on the format of `changed_context.py` output; keep that contract stable.
- If the repo has no git history (or only one commit), use `changed_context.py --files <path>` with a fixture file list, or set `REF` to an existing ref.

## Docgen E2E Pipeline (Fixed Target Docs: README + book)

**Purpose**: End-to-end flow from code change to updated **target docs** (README.md and book/src/**/*.md only). **Deterministic** steps: change_facts (JSON), reference_context, verify. **AI** stages: doc_scope_decision, doc_restructure_or_update, review, E2E summary. **Reference context** (examples, tests, templates, source) is collected for Agent use only; it is not a list of target docs.

**Target docs vs reference**: Only **README.md** and **book/src/**\*.md** are updated. Examples, tests, templates, and other docs are **reference material** only.

**Codex requirement**: The E2E pipeline **requires** the Codex CLI for Agent stages. If `codex` is not in `PATH`, `run_docgen_e2e.sh` exits with 1.

**Data flow**: [docs/docgen_e2e_flow.md](../../docs/docgen_e2e_flow.md).

**Entry points**:

```bash
bash scripts/docgen/run_docgen_e2e.sh
REF=main bash scripts/docgen/run_docgen_e2e.sh   # optional
# Closed loop (verify-repair + review-rework with limits):
bash scripts/docgen/run_docgen_e2e_closed.sh
```

**Pipeline stages**:

| Phase | Steps |
|-------|--------|
| **Deterministic** | (1–2b) Rules, skills, codex; (3) `change_facts.py` → `change_facts.json`; (5) `reference_context.py` → `reference_context.json`; (7) `run_verify_report.py` → `verify_report.json` on target docs, including blocking Mermaid render verify plus diagram screenshot artifacts when Mermaid is present. |
| **AI (codex exec)** | (4) doc_scope_decision → `doc_scope_decision.json`; (6) doc_restructure_or_update → edit README + book in place; (8) review → `docgen_review_report.md`. |
| **Deterministic** | (9) `render_docgen_e2e_summary.py` → `docgen_e2e_summary.md` (evidence-only; no Codex). Closed loop uses the same renderer after `e2e_run_evidence.json` is written. |

Task files: `ai_doc_scope_decision.md`, `ai_doc_restructure_or_update.md`, `ai_review.md`. Step 9 summary is produced by `render_docgen_e2e_summary.py` (no task file).

**Structured artifacts (JSON)**:

| Artifact | Path | Producer |
|----------|------|----------|
| Change facts | `docs/generated/change_facts.json` | change_facts.py |
| Doc scope decision | `docs/generated/doc_scope_decision.json` | Agent (ai_doc_scope_decision) |
| Reference context | `docs/generated/reference_context.json` | reference_context.py |
| Verify report | `docs/generated/verify_report.json` | run_verify_report.py (includes `blocking_issues` / `non_blocking_issues` when failed, plus Mermaid artifact report paths per file) |
| Diagram specs | `docs/generated/diagram_specs/...` | Agent writer stage when Mermaid is added or revised |
| Diagram artifacts | `docs/generated/diagram_artifacts/...` | verify_mermaid.py (SVG, PNG, and per-doc `report.json`) |
| Verify repair report | `docs/generated/verify_repair_report.json` | Agent (ai_verify_repair), closed loop only |
| Review rework report | `docs/generated/review_rework_report.json` | Agent (ai_review_rework), closed loop only |
| E2E loop state | `docs/generated/e2e_loop_state.json` | run_docgen_e2e_loop.py |
| E2E run evidence | `docs/generated/e2e_run_evidence.json` | run_docgen_e2e_loop.py |

**Other outputs**: `docgen_validation_report.md`, `docgen_review_report.md` (optional `docgen_review_report.json`), `docgen_e2e_summary.md`. On **TerminalFail** in closed loop: `e2e_failure_report.md` (failure type and paths to last verify/review reports plus the run evidence report). Target docs (README, book) are edited in place by the Agent.

**Mermaid bootstrap contract**:

- Mermaid render verification is now part of the blocking verify surface, alongside paths and links.
- The first Mermaid-bearing verify run bootstraps a local Mermaid bundle under `tmp/mermaid-tools/`, a Python virtual environment under `.venv/`, Python `playwright`, and Playwright `chromium`.
- Those bootstrap products are local cache/runtime assets, not destination docs.
- Review is expected to consume the emitted screenshot artifacts under `docs/generated/diagram_artifacts/`, not just Mermaid source text.

**Local reproduction** (full E2E):

```bash
bash scripts/docgen/setup.sh
# Install/configure Codex CLI, then:
bash scripts/docgen/run_docgen_e2e.sh
# Or run the bounded closed loop:
bash scripts/docgen/run_docgen_e2e_closed.sh
# Open docs/generated/docgen_e2e_summary.md for human review.
```

**Independent codex verification** (`run_docgen_verify.sh`): (1) Read-only flow understanding → `docgen_flow_understanding.txt`; (2) Black-box execution (run E2E first); (3) Fixture classification → `docgen_fixture_verification.txt`. Run: `bash scripts/docgen/run_docgen_verify.sh`.

**E2E tests**:

```bash
python3 -m unittest tests.test_docgen_e2e
```

- **No-codex test**: When `codex` is not in PATH, `run_docgen_e2e.sh` must exit non-zero and stderr/stdout must contain a clear "codex" or "required" style message.
- **With codex** (optional): Full E2E can be run; tests may skip if codex is unavailable.

**Known limitations**:

- **Depends on Codex CLI**; no automatic fallback.
- Candidate and review quality depend on the model and task descriptions.
- Mermaid bootstrap depends on `npm`, Python venv support, and the ability to install Playwright + Chromium on first use.
- No automatic edits to real docs beyond the approved README/book targets; no CI/commit/push from the pipeline itself.

## Common Interface

All Python scripts share a small set of conventions:

- `--root`: repository root directory (default: current working directory).
- Exit code `0`: success. Exit code `1`: errors found or invalid input.

The script families then add their own arguments:

```bash
python3 scripts/docgen/<inventory>.py [--root <repo-root>] [--output <path>]
python3 scripts/docgen/change_facts.py [--root <repo-root>] [--ref <git-ref>] [--files <path>] [--output <path>]
python3 scripts/docgen/reference_context.py [--root <repo-root>] [--scope <path>] [--facts <path>] [--output <path>]
python3 scripts/docgen/changed_context.py [--root <repo-root>] [--ref <git-ref>] [--files <path>] [--format json|md] [--output <path>]
python3 scripts/docgen/verify_<name>.py <doc-file> [--root <repo-root>]
```

## Local Reproduction

```bash
# Prerequisites
bash scripts/docgen/setup.sh

# Full pipeline
bash scripts/docgen/run_repo_understanding.sh

# Individual scripts
python3 scripts/docgen/repo_map.py --root .
python3 scripts/docgen/doc_inventory.py --root .
python3 scripts/docgen/example_inventory.py --root .
python3 scripts/docgen/repo_map.py --root . --output docs/generated/repo_map_output.md
python3 scripts/docgen/doc_inventory.py --root . --output docs/generated/doc_inventory_output.md
python3 scripts/docgen/example_inventory.py --root . --output docs/generated/example_inventory_output.md
python3 scripts/docgen/verify_paths.py README.md --root .
python3 scripts/docgen/verify_links.py README.md --root .
python3 scripts/docgen/verify_commands.py README.md --root .
python3 scripts/docgen/verify_doc_consistency.py README.md --root .

# Mermaid render + screenshot artifacts
python3 scripts/docgen/verify_mermaid.py book/src/01-overview.md --root .
python3 scripts/docgen/verify_mermaid.py book/src/03-http-llm-streaming.md --root .

# Regression tests
python3 -m unittest tests.test_docgen_verify tests.test_docgen_verify_consistency_links tests.test_docgen_change_impact tests.test_docgen_e2e tests.test_docgen_e2e_loop tests.test_docgen_style_module
```

`setup.sh` verifies host prerequisites only. Mermaid render verification still bootstraps `.venv/`, Playwright, Chromium, and `tmp/mermaid-tools/` on first use. It reports whether `pytest` is present, but the bundled regression tests run with `unittest` and do not require pytest itself.

## Relationship to Skills

Scripts provide **data**; skills provide **judgment**:

1. Scripts scan, extract, and verify deterministically.
2. Skills consume script output and make decisions about what to write or update.
3. The model does the actual understanding, synthesis, and writing.

See `AGENTS.md` → "Responsibility Split" for the full breakdown.
