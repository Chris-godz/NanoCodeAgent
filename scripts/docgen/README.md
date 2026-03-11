# Docgen Scripts

Deterministic scripts that support the documentation automation workflow. These scripts handle scanning, extraction, and verification — tasks that should NOT be left to the model to guess.

## Script Inventory

| Script | Purpose | Status | Blocking? |
|--------|---------|--------|-----------|
| `setup.sh` | Verify environment prerequisites (Python 3, git) | Functional | Yes |
| `run_repo_understanding.sh` | Unified entry point for repo-understanding pipeline | Functional | Yes |
| `repo_map.py` | Generate repository structure map | Functional | Yes |
| `doc_inventory.py` | Index documentation files and assess staleness | Functional | Yes |
| `example_inventory.py` | Map examples to tutorial sections | Functional | Yes |
| `changed_context.py` | Collect context around changed files | Functional | Yes |
| `verify_paths.py` | Check that documented paths exist | Functional | Yes |
| `verify_links.py` | Validate internal Markdown links and anchors | Functional | Yes |
| `verify_doc_consistency.py` | Check path/script references against repo | Minimal | No |
| `verify_commands.py` | Validate shell command syntax via `bash -n` | Minimal | No |

### Status Legend

- **Functional** — complete implementation, exit codes reliable.
- **Minimal** — core checks implemented, some verification categories NOT_IMPLEMENTED.

### Blocking vs Non-Blocking

**Blocking** scripts must pass (exit 0) for the pipeline to continue. If a blocking script fails, the pipeline halts and the error must be fixed.

**Non-blocking** scripts report findings but do not gate the pipeline. Their exit codes are logged but ignored by `run_repo_understanding.sh`.

## Unified Entry Point

```bash
bash scripts/docgen/run_repo_understanding.sh
```

This runs the full repo-understanding pipeline in order:
1. Read AGENTS.md documentation automation rules (targeted `sed` extract).
2. Verify skill file exists.
3. Run `repo_map.py` and `doc_inventory.py`.
4. Check for existing summary (skip regeneration if present).
5. Run blocking verifiers (`verify_paths.py`, `verify_links.py`).
6. Run non-blocking verifiers (`verify_doc_consistency.py`, `verify_commands.py`).
7. Audit generated output for absolute paths.

## Common Interface

All Python scripts follow the same conventions:

```bash
python3 scripts/docgen/<script>.py [--root <repo-root>] [--output <path>]
```

- `--root`: repository root directory (default: current working directory).
- `--output`: write output to a file instead of stdout.
- Exit code `0`: success. Exit code `1`: errors found or invalid input.

## Local Reproduction

```bash
# Prerequisites
bash scripts/docgen/setup.sh

# Full pipeline
bash scripts/docgen/run_repo_understanding.sh

# Individual scripts
python3 scripts/docgen/repo_map.py --root .
python3 scripts/docgen/doc_inventory.py --root .
python3 scripts/docgen/verify_paths.py README.md --root .
python3 scripts/docgen/verify_links.py README.md --root .
python3 scripts/docgen/verify_commands.py README.md --root .
python3 scripts/docgen/verify_doc_consistency.py README.md --root .

# Regression tests
python3 -m pytest tests/test_docgen_verify.py -v
```

## Relationship to Skills

Scripts provide **data**; skills provide **judgment**:

1. Scripts scan, extract, and verify deterministically.
2. Skills consume script output and make decisions about what to write or update.
3. The model does the actual understanding, synthesis, and writing.

See `AGENTS.md` → "Responsibility Split" for the full breakdown.
