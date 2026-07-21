#!/usr/bin/env python3
"""Synchronize the plugin version. Usage: python scripts/bump_version.py [--dry-run] <version>."""

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOTH = ROOT / "tooth.json"
SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)


@dataclass(frozen=True)
class Target:
    path: str
    mode: str
    required: bool = False
    pattern: str = ""


TARGETS = (
    Target("tooth.json", "tooth", required=True),
    Target(
        "src/mod/ServerInfoRestMod.cpp",
        "declaration",
        required=True,
        pattern=r'(?m)^(\s*constexpr\s+auto\s+PluginVersion\s*=\s*"){old}("\s*;.*)$',
    ),
)


class Style:
    BOLD = "\033[1m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    RED = "\033[91m"
    CYAN = "\033[96m"
    DIM = "\033[2m"
    RESET = "\033[0m"


def read_text(path: Path) -> str:
    with path.open("r", encoding="utf-8", newline="") as file:
        return file.read()


def write_text(path: Path, content: str) -> None:
    with path.open("w", encoding="utf-8", newline="") as file:
        file.write(content)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Synchronize the plugin version across repository files.")
    parser.add_argument("new_version", help="new semantic version, for example X.Y.Z-beta.W+YYYYMMDD")
    parser.add_argument(
        "-d",
        "--dryrun",
        "--dry-run",
        dest="dry_run",
        action="store_true",
        help="show changes without writing files",
    )
    return parser.parse_args()


def load_old_version() -> str:
    try:
        data = json.loads(read_text(TOOTH))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {TOOTH}: {error}") from error
    version = data.get("version")
    if not isinstance(version, str) or not version:
        raise ValueError('tooth.json does not contain a non-empty top-level "version"')
    return version


def replace_target(target: Target, content: str, old: str, new: str) -> tuple[str, int]:
    if target.mode == "tooth":
        pattern = re.compile(rf'(?m)^(\s*"version"\s*:\s*"){re.escape(old)}("\s*,?\s*)$')
    elif target.mode == "declaration":
        pattern = re.compile(target.pattern.format(old=re.escape(old)))
    else:
        raise ValueError(f"unsupported target mode: {target.mode}")
    return pattern.subn(lambda match: f"{match.group(1)}{new}{match.group(2)}", content)


def plan_changes(old: str, new: str) -> tuple[list[tuple[Path, str, int]], list[str], list[str]]:
    changes: list[tuple[Path, str, int]] = []
    warnings: list[str] = []
    errors: list[str] = []
    for target in TARGETS:
        path = ROOT / target.path
        if not path.is_file():
            message = f"{target.path}: file not found"
            (errors if target.required else warnings).append(message)
            continue
        content = read_text(path)
        updated, count = replace_target(target, content, old, new)
        if count == 0:
            message = f"{target.path}: current version {old} not found"
            (errors if target.required else warnings).append(message)
            continue
        changes.append((path, updated, count))
    return changes, warnings, errors


def main() -> int:
    args = parse_args()
    try:
        old = load_old_version()
    except ValueError as error:
        print(f"{Style.RED}error:{Style.RESET} {error}", file=sys.stderr)
        return 2

    new = args.new_version.strip()
    if not SEMVER.fullmatch(new):
        print(f"{Style.RED}error:{Style.RESET} invalid semantic version: {new}", file=sys.stderr)
        return 2
    if old == new:
        print(f"{Style.YELLOW}Already at {old}; nothing to change.{Style.RESET}")
        return 0

    changes, warnings, errors = plan_changes(old, new)
    print(f"\n{Style.BOLD}Version {old} -> {new}{Style.RESET}")
    if args.dry_run:
        print(f"{Style.CYAN}Dry run: no files will be written.{Style.RESET}")
    print()
    for path, _, count in changes:
        print(f"  {Style.GREEN}{'would update' if args.dry_run else 'update'}{Style.RESET} "
              f"{path.relative_to(ROOT)} ({count} occurrence{'s' if count != 1 else ''})")
    for warning in warnings:
        print(f"  {Style.YELLOW}warning:{Style.RESET} {warning}")
    for error in errors:
        print(f"  {Style.RED}error:{Style.RESET} {error}")

    if errors:
        print(f"\n{Style.RED}No files were written because required targets failed validation.{Style.RESET}")
        return 2
    if not args.dry_run:
        for path, content, _ in changes:
            write_text(path, content)
        print(f"\n{Style.GREEN}{Style.BOLD}Updated {len(changes)} files.{Style.RESET}")
    else:
        print(f"\n{Style.DIM}Validated {len(changes)} files; no files were written.{Style.RESET}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
