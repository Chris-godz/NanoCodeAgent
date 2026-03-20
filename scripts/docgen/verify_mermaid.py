#!/usr/bin/env python3
"""verify_mermaid.py — Render Mermaid fences and fail on parse errors.

Purpose:
    Extract Mermaid fenced code blocks from a Markdown document and verify
    that each one can be rendered for real. Syntax and render failures are
    treated as blocking verification errors.

Behavior:
    - Mermaid fences must be properly closed.
    - Each Mermaid block is rendered through a local Playwright + Chromium
      check using a local Mermaid JS bundle.
    - Parse or render failures exit with code 1.

Usage:
    python3 scripts/docgen/verify_mermaid.py <doc-file> [--root <repo-root>]
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


MERMAID_BUNDLE = Path("tmp/mermaid-tools/node_modules/mermaid/dist/mermaid.min.js")
VENV_PYTHON = Path(".venv/bin/python")
PLAYWRIGHT_HELPER_FLAG = "--playwright-helper"
DEFAULT_ARTIFACTS_ROOT = Path("docs/generated/diagram_artifacts")


def extract_mermaid_blocks(content: str) -> tuple[list[dict[str, object]], list[str]]:
    """Return Mermaid blocks with start lines, plus fence errors."""
    blocks: list[dict[str, object]] = []
    errors: list[str] = []
    lines = content.splitlines()

    in_block = False
    start_line = 0
    collected: list[str] = []

    for idx, line in enumerate(lines, start=1):
        stripped = line.strip()
        if not in_block and stripped == "```mermaid":
            in_block = True
            start_line = idx
            collected = []
            continue

        if in_block and stripped == "```":
            code = "\n".join(collected).strip("\n")
            if not code.strip():
                errors.append(f"line {start_line}: mermaid fence is empty")
            else:
                blocks.append({
                    "start_line": start_line,
                    "code": code,
                })
            in_block = False
            start_line = 0
            collected = []
            continue

        if in_block:
            collected.append(line)

    if in_block:
        errors.append(f"line {start_line}: mermaid fence is not closed")

    return blocks, errors


def ensure_mermaid_bundle(root: Path) -> Path:
    bundle = root / MERMAID_BUNDLE
    if bundle.is_file():
        return bundle

    tools_dir = root / "tmp" / "mermaid-tools"
    tools_dir.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        ["npm", "install", "--prefix", str(tools_dir), "mermaid"],
        cwd=root,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 or not bundle.is_file():
        stderr = (result.stderr or "").strip()
        stdout = (result.stdout or "").strip()
        detail = stderr or stdout or "npm install mermaid failed"
        raise RuntimeError(f"unable to install local Mermaid bundle: {detail}")
    return bundle


def ensure_playwright_python(root: Path) -> Path:
    venv_dir = root / ".venv"
    venv_python = root / VENV_PYTHON
    if venv_python.is_file():
        try:
            check = subprocess.run(
                [str(venv_python), "-c", "import playwright"],
                cwd=root,
                capture_output=True,
                text=True,
            )
        except OSError:
            check = None
        if check is not None and check.returncode == 0:
            return venv_python

    def create_venv(clean: bool) -> subprocess.CompletedProcess[str]:
        if clean and venv_dir.exists():
            shutil.rmtree(venv_dir, ignore_errors=True)
        return subprocess.run(
            [
                "python3",
                "-m",
                "venv",
                ".venv",
            ],
            cwd=root,
            capture_output=True,
            text=True,
        )

    result = create_venv(clean=False)
    if result.returncode != 0:
        result = create_venv(clean=True)
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        raise RuntimeError(f"unable to create .venv for Mermaid verify: {detail}")

    install = subprocess.run(
        [
            str(venv_python),
            "-m",
            "pip",
            "install",
            "playwright",
        ],
        cwd=root,
        capture_output=True,
        text=True,
    )
    if install.returncode != 0:
        detail = (install.stderr or install.stdout or "").strip()
        raise RuntimeError(f"unable to install playwright: {detail}")

    browser = subprocess.run(
        [
            str(venv_python),
            "-m",
            "playwright",
            "install",
            "chromium",
        ],
        cwd=root,
        capture_output=True,
        text=True,
    )
    if browser.returncode != 0:
        detail = (browser.stderr or browser.stdout or "").strip()
        raise RuntimeError(f"unable to install chromium for Mermaid verify: {detail}")

    return venv_python


def slugify_doc_path(rel_doc: str) -> str:
    normalized = rel_doc.replace("\\", "/")
    if normalized.endswith(".md"):
        normalized = normalized[:-3]
    return normalized.replace("/", "__")


def repo_rel(path: Path, root: Path) -> str:
    return os.path.relpath(path, root).replace("\\", "/")


def build_artifact_plan(
    root: Path,
    rel_doc: str,
    blocks: list[dict[str, object]],
    artifacts_root: Path,
) -> tuple[Path, list[dict[str, object]], Path]:
    slug = slugify_doc_path(rel_doc)
    artifact_dir = root / artifacts_root / slug
    artifact_dir.mkdir(parents=True, exist_ok=True)
    planned: list[dict[str, object]] = []
    for index, block in enumerate(blocks, start=1):
        planned.append(
            {
                "index": index,
                "start_line": block["start_line"],
                "code": block["code"],
                "svg_path": str((artifact_dir / f"block-{index:02d}.svg").resolve()),
                "screenshot_path": str((artifact_dir / f"block-{index:02d}.png").resolve()),
            }
        )
    report_path = artifact_dir / "report.json"
    return artifact_dir, planned, report_path


def render_with_playwright(root: Path, bundle: Path, blocks: list[dict[str, object]]) -> list[dict[str, object]]:
    python_exe = ensure_playwright_python(root)

    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False, encoding="utf-8") as tmp:
        json.dump(
            {
                "bundle": str(bundle),
                "blocks": blocks,
            },
            tmp,
            ensure_ascii=False,
        )
        tmp.write("\n")
        tmp_path = tmp.name

    try:
        result = subprocess.run(
            [str(python_exe), __file__, PLAYWRIGHT_HELPER_FLAG, tmp_path],
            cwd=root,
            capture_output=True,
            text=True,
        )
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass

    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        raise RuntimeError(f"playwright Mermaid render failed: {detail}")

    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"invalid Mermaid render helper output: {exc}") from exc


def run_playwright_helper(input_path: str) -> None:
    from playwright.async_api import async_playwright
    import asyncio

    with open(input_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    bundle = payload["bundle"]
    blocks = payload["blocks"]

    async def _run() -> list[dict[str, object]]:
        results: list[dict[str, object]] = []
        async with async_playwright() as p:
            browser = await p.chromium.launch()
            page = await browser.new_page()
            await page.set_content("<html><body></body></html>")
            await page.add_script_tag(path=bundle)
            await page.wait_for_function("() => !!window.mermaid")
            await page.evaluate(
                "window.mermaid.initialize({ startOnLoad: false, securityLevel: 'strict' })"
            )

            for idx, block in enumerate(blocks, start=1):
                rendered = await page.evaluate(
                    """
async ({ code, id }) => {
  try {
    const out = await window.mermaid.render(id, code);
    return { ok: true, svg: out.svg };
  } catch (error) {
    const message = error && error.message ? error.message : String(error);
    return { ok: false, error: message };
  }
}
                    """,
                    {
                        "code": block["code"],
                        "id": f"docgen_mermaid_{idx}",
                    },
                )
                results.append(
                    {
                        "index": block["index"],
                        "start_line": block["start_line"],
                        "ok": bool(rendered.get("ok")),
                        "error": rendered.get("error", ""),
                        "has_svg": "<svg" in rendered.get("svg", ""),
                    }
                )
                if rendered.get("ok") and "<svg" in rendered.get("svg", ""):
                    svg_path = Path(block["svg_path"])
                    png_path = Path(block["screenshot_path"])
                    svg_path.parent.mkdir(parents=True, exist_ok=True)
                    svg_path.write_text(rendered["svg"], encoding="utf-8")
                    await page.evaluate(
                        """
({ svg }) => {
  document.body.innerHTML = `<div id="diagram-shot" style="display:inline-block;padding:24px;background:white">${svg}</div>`;
}
                        """,
                        {"svg": rendered["svg"]},
                    )
                    await page.locator("#diagram-shot").screenshot(path=str(png_path))
                    results[-1]["svg_path"] = str(svg_path)
                    results[-1]["screenshot_path"] = str(png_path)

            await browser.close()
        return results

    results = asyncio.run(_run())
    print(json.dumps(results, ensure_ascii=False))


def main() -> None:
    if len(sys.argv) >= 2 and sys.argv[1] == PLAYWRIGHT_HELPER_FLAG:
        if len(sys.argv) != 3:
            print("helper requires a JSON input path", file=sys.stderr)
            sys.exit(1)
        run_playwright_helper(sys.argv[2])
        return

    parser = argparse.ArgumentParser(description="Render-check Mermaid diagrams in documentation.")
    parser.add_argument("doc_file", help="Markdown file to check.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    parser.add_argument(
        "--artifacts-root",
        default=str(DEFAULT_ARTIFACTS_ROOT),
        help="Repository-relative artifact directory for rendered diagram outputs.",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    doc_file = Path(args.doc_file)
    if not doc_file.is_absolute():
        doc_file = (root / doc_file).resolve()

    if not doc_file.is_file():
        print(f"ERROR: {doc_file} not found.", file=sys.stderr)
        sys.exit(1)

    content = doc_file.read_text(encoding="utf-8")
    rel_doc = os.path.relpath(doc_file, root).replace("\\", "/")
    blocks, fence_errors = extract_mermaid_blocks(content)
    artifacts_root = Path(args.artifacts_root)

    if not blocks and not fence_errors:
        print(f"OK: no mermaid code blocks found in {rel_doc}.")
        sys.exit(0)

    if fence_errors:
        for error in fence_errors:
            print(f"  FAIL  {error}")
        print(f"\nFAILED: Mermaid fence errors detected in {rel_doc}.")
        sys.exit(1)

    try:
        bundle = ensure_mermaid_bundle(root)
        artifact_dir, planned_blocks, report_path = build_artifact_plan(
            root, rel_doc, blocks, artifacts_root
        )
        results = render_with_playwright(root, bundle, planned_blocks)
    except RuntimeError as exc:
        print(f"FAILED: Mermaid renderer unavailable for {rel_doc}.")
        print(f"- {exc}")
        sys.exit(1)

    failures: list[str] = []
    for index, result in enumerate(results, start=1):
        line = result["start_line"]
        if result["ok"] and result["has_svg"]:
            print(f"  PASS  block {index} (line {line})")
            continue

        error = str(result.get("error") or "render returned no SVG")
        error = " ".join(error.split())
        failures.append(f"line {line}: {error}")
        print(f"  FAIL  block {index} (line {line}): {error}")

    artifact_report = {
        "file": rel_doc,
        "artifact_dir": repo_rel(artifact_dir, root),
        "blocks": [
            {
                "index": result["index"],
                "start_line": result["start_line"],
                "ok": result["ok"],
                "error": result["error"],
                "svg_path": repo_rel(Path(result["svg_path"]), root)
                if result.get("svg_path")
                else "",
                "screenshot_path": repo_rel(Path(result["screenshot_path"]), root)
                if result.get("screenshot_path")
                else "",
            }
            for result in results
        ],
    }
    report_path.write_text(
        json.dumps(artifact_report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    if failures:
        print(f"\nFAILED: {len(failures)} Mermaid block(s) failed to render in {rel_doc}.")
        for failure in failures:
            print(f"- {failure}")
        print(f"ARTIFACTS: {repo_rel(report_path, root)}")
        sys.exit(1)

    print(f"\nOK: all {len(blocks)} Mermaid block(s) render successfully in {rel_doc}.")
    print(f"ARTIFACTS: {repo_rel(report_path, root)}")


if __name__ == "__main__":
    main()
