from __future__ import annotations

from pathlib import Path
import os
import subprocess


def _git(project_dir: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(project_dir), *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def read_base_version(project_dir: Path) -> str:
    text = (project_dir / "include" / "config.h").read_text(encoding="utf-8")
    for line in text.splitlines():
        if line.startswith('#define APP_VERSION_BASE '):
            return line.split('"', 2)[1]
    raise ValueError("APP_VERSION_BASE konnte in include/config.h nicht gefunden werden")


def get_git_short_hash(project_dir: Path) -> str | None:
    try:
        return _git(project_dir, "rev-parse", "--short", "HEAD")
    except Exception:
        return None


def get_git_branch(project_dir: Path) -> str | None:
    try:
        return _git(project_dir, "branch", "--show-current")
    except Exception:
        return None


def is_production_build(project_dir: Path) -> bool:
    mode = os.environ.get("SQUEZZIE_BUILD_MODE", "").strip().lower()
    if mode in {"production", "release", "prod"}:
        return True
    branch = get_git_branch(project_dir)
    return bool(branch and branch.startswith("release/"))


def build_version_string(project_dir: Path) -> str:
    base = read_base_version(project_dir)
    if is_production_build(project_dir):
        return base
    short_hash = get_git_short_hash(project_dir)
    if short_hash:
        return f"{base}+{short_hash}"
    return f"{base}+dev"
