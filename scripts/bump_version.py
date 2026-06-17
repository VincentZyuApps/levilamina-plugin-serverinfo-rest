#!/usr/bin/env python3
"""
bump_version.py — 批量更新版本号
扫描 5 处硬编码的版本号，一键替换为新版本。
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


class S:
    BOLD = "\033[1m"
    ITALIC = "\033[3m"
    RST = "\033[0m"


class C:
    RED = "\033[31m"
    GRN = "\033[32m"
    YLW = "\033[33m"
    CYN = "\033[36m"
    GRY = "\033[90m"
    BRED = "\033[91m"
    BGRE = "\033[92m"
    BYLW = "\033[93m"


def st(text, *styles):
    return "".join(styles) + text + S.RST


FILES = [
    ("tooth.json", r'"version":\s*"([^"]+)"', r'"version": "{}"'),
    ("README.md", r'"version":\s*"([^"]+)"', r'"version": "{}"'),
    (
        "src/mod/ServerInfoRestMod.cpp",
        r'json\["version"\]\s*=\s*"([^"]+)"',
        r'json["version"] = "{}"',
    ),
]


def detect() -> str:
    m = re.search(
        r'"version":\s*"([^"]+)"', (REPO_ROOT / "tooth.json").read_text("utf-8")
    )
    return m.group(1) if m else "?"


def run(old: str, new: str):
    total = 0
    changed = 0
    skipped = 0
    for path, pat, tpl in FILES:
        fp = REPO_ROOT / path
        content = fp.read_text("utf-8")
        new_content, n = re.subn(pat, lambda m: tpl.format(new), content)
        if n:
            fp.write_text(new_content, "utf-8")
            total += n
            changed += 1
            icon = st("✓", C.BGRE, S.BOLD)
            label = st("changed", C.GRN, S.ITALIC)
            print(f"  {icon} {st(path, C.CYN, S.BOLD):44s} {label}    ({n}处)")
        else:
            skipped += 1
            print(
                f"  {st('·', C.GRY)} {st(path, C.GRY):44s} {st('unchanged', C.GRY, S.ITALIC)}"
            )
    return total, changed, skipped


def main():
    if len(sys.argv) != 2:
        print(
            f"  {st('✖', C.RED, S.BOLD)} 用法: python scripts/bump_version.py {st('<版本号>', C.CYN, S.ITALIC)}"
        )
        print(f"  {st('例:', C.GRY)}  python scripts/bump_version.py 1.0.1")
        sys.exit(1)

    new = sys.argv[1].strip()
    if not re.match(r"^[\d][\w.]*(?:-\w+(?:\.\w+)*)?$", new):
        print(f"  {st('✖', C.BRED, S.BOLD)} 无效版本号: {new}")
        sys.exit(1)

    old = detect()
    if old == new:
        print(
            f"\n  {st('🔧 Bump Version', S.BOLD)}  {st(old, C.BYLW, S.BOLD, S.ITALIC)} {st('→', C.GRY)} {st(new, C.GRN, S.BOLD, S.ITALIC)}\n"
        )
        print(f"  {st('版本未变化，无需更新', C.GRY, S.ITALIC)}\n")
        return

    print(
        f"\n  {st('🔧 Bump Version', S.BOLD)}  {st(old, C.BYLW, S.BOLD, S.ITALIC)} {st('→', C.GRY)} {st(new, C.BGRE, S.BOLD, S.ITALIC)}\n"
    )

    total, changed, skipped = run(old, new)

    print(
        f"\n  {st('Done.', C.GRN, S.BOLD)}  {st(f'{total} 处更新 · {changed} 文件改 · {skipped} 文件跳过', C.GRY, S.ITALIC)}\n"
    )


if __name__ == "__main__":
    main()
