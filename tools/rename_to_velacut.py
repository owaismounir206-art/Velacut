#!/usr/bin/env python3
"""One-shot rename vedit -> Velacut (PR1). Keeps persisted identifiers (effect/pack/format ids) stable."""
import re
import subprocess
import sys

# "vedit.<x>" tokens that are NOT persisted data: they get renamed.
RENAMED_DOTTED = {"engine", "ui", "log", "desktop", "1", "git", "document", "theme", "svg", "render", "gpu", "app",
                  "project", "proj"}

files = subprocess.run(["git", "ls-files"], capture_output=True, text=True, check=True).stdout.split("\n")
binary_ext = (".svg", ".gif", ".png", ".woff2", ".ttf", ".jpg", ".webp")
skip_prefix = ("third_party/", "resources/packs/vedit.core/stickers/")

dotted = re.compile(r"vedit\.([A-Za-z0-9_]+)")


def transform(text: str) -> str:
    # 1. Protect persisted dotted ids.
    protected = []

    def protect(m):
        if m.group(1) in RENAMED_DOTTED:
            return "velacut." + m.group(1)
        protected.append(m.group(0))
        return f"\x00{len(protected) - 1}\x00"

    text = dotted.sub(protect, text)
    # The pack folder path "packs/vedit.core" is protected above (vedit.core). Now the rest.
    text = text.replace("VEDIT", "VELACUT").replace("Vedit", "Velacut").replace("vedit", "velacut")
    text = re.sub(r"\x00(\d+)\x00", lambda m: protected[int(m.group(1))], text)
    return text


changed = 0
for path in files:
    if not path or path.endswith(binary_ext) or path.startswith(skip_prefix) or path == "tools/rename_to_velacut.py":
        continue
    try:
        with open(path, encoding="utf-8") as f:
            original = f.read()
    except (UnicodeDecodeError, FileNotFoundError):
        continue
    updated = transform(original)
    if updated != original:
        with open(path, "w", encoding="utf-8") as f:
            f.write(updated)
        changed += 1

renames = {
    "cmake/VeditCompilerOptions.cmake": "cmake/VelacutCompilerOptions.cmake",
    "docs/vedit.1": "docs/velacut.1",
    "i18n/vedit_en.ts": "i18n/velacut_en.ts",
    "i18n/vedit_it.ts": "i18n/velacut_it.ts",
    "packaging/vedit.desktop": "packaging/velacut.desktop",
}
for src, dst in renames.items():
    subprocess.run(["git", "mv", src, dst], check=True)
print(f"{changed} files rewritten, {len(renames)} files renamed", file=sys.stderr)
