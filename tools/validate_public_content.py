#!/usr/bin/env python3
"""Fast, dependency-free checks for the public landing content."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
REQUIRED = ["index.md", "get-started.md", "hardware.md", "user-guide.md", "troubleshooting.md", "safety.md", "faq.md", "developers.md", "about.md", "404.html"]
ROUTES = ["index.html", "get-started/index.html", "hardware/index.html", "user-guide/index.html", "troubleshooting/index.html", "safety/index.html", "faq/index.html", "developers/index.html", "about/index.html", "404.html"]
MARKDOWN_SOURCES = [ROOT / "README.md", ROOT / "docs/README.md", ROOT / "docs/USER_GUIDE.md"]
REQUIRED_IMAGES = ["hero-dashboard.png", "hardware-page.png", "calibration-page.png", "sequence-page.png", "social-preview.png", "system-overview.svg"]

def fail(errors: list[str], message: str) -> None:
    errors.append(message)

def check_local_links(path: Path, errors: list[str]) -> None:
    for target in re.findall(r"\]\(([^)#]+)", path.read_text(encoding="utf-8")):
        if "://" in target or target.startswith(("mailto:", "{{", "/")):
            continue
        target_path = (path.parent / target).resolve()
        if not target_path.exists():
            fail(errors, f"broken local Markdown link in {path.relative_to(ROOT)}: {target}")

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--built", type=Path)
    args = parser.parse_args()
    errors: list[str] = []
    for name in REQUIRED:
        if not (SITE / name).is_file():
            fail(errors, f"missing required site page: {name}")
    project = (SITE / "_data/project.yml").read_text(encoding="utf-8")
    if "OpenTurbineSetupTool.exe" not in project:
        fail(errors, "project data does not name the stable setup-tool asset")
    if "releases/latest/download/OpenTurbineSetupTool.exe" not in project:
        fail(errors, "project data does not use the stable release download URL")
    public_text = "\n".join(p.read_text(encoding="utf-8") for p in SITE.rglob("*") if p.is_file() and p.suffix in {".md", ".html", ".yml"})
    for placeholder in ("TODO", "TBD", "example.com"):
        if placeholder in public_text:
            fail(errors, f"public content contains placeholder: {placeholder}")
    for image in re.findall(r"/assets/images/([\w.-]+)", public_text):
        if not (SITE / "assets/images" / image).is_file():
            fail(errors, f"referenced image is missing: {image}")
    for image in REQUIRED_IMAGES:
        if not (SITE / "assets/images" / image).is_file():
            fail(errors, f"required public image is missing: {image}")
    for image in (SITE / "assets/images").glob("*.png"):
        if image.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
            fail(errors, f"PNG extension does not match image encoding: {image.name}")
    for obsolete in ("OpenTurbine#first-setup", "OpenTurbine#electrical-and-wiring-basics", "openturbine-logo.svg"):
        if obsolete in public_text:
            fail(errors, f"public content contains obsolete reference: {obsolete}")
    layout = (SITE / "_layouts/default.html").read_text(encoding="utf-8")
    if "canonical" not in layout or "og:image" not in layout or "twitter:card" not in layout:
        fail(errors, "default layout is missing sharing metadata")
    header = (SITE / "_includes/header.html").read_text(encoding="utf-8")
    if "aria-current=\"page\"" not in header:
        fail(errors, "header is missing active-page navigation state")
    for markdown in MARKDOWN_SOURCES:
        check_local_links(markdown, errors)
    if args.built:
        for route in ROUTES:
            if not (args.built / route).is_file():
                fail(errors, f"missing generated route: {route}")
    if errors:
        print("Public content validation failed:", *[f"- {e}" for e in errors], sep="\n", file=sys.stderr)
        return 1
    print("Public content validation passed.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
