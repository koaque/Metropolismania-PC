#!/usr/bin/env python3
"""Check the struct layouts in config/types/*.h with the host C compiler.

usage: check_types.py

Every field comment that starts with "+0xNN" becomes a static assertion on the field's offset, and a
"(0xNN bytes)" note in the comment just above a struct becomes one on its size. Pointer fields are
compiled as uint32_t, so the layout is the guest's (32-bit pointers) on a 64-bit host. Uses MSVC
through the same vcvars64 environment as pipeline.py build.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

FIELD = re.compile(r"^(?P<indent>\s+)(?P<type>[A-Za-z_][\w ]*?)\s*(?P<ptr>\*+)\s*(?P<name>\w+)(?P<arr>\[[^\]]+\])?;")
NAMED = re.compile(r"^\s+[A-Za-z_][\w ]*?[\s*]+(?P<name>\w+)(\[[^\]]+\])?;\s*/\*\s*\+(?P<off>0x[0-9A-Fa-f]+)")
STRUCT = re.compile(r"^struct (\w+) \{")
SIZE = re.compile(r"\((0x[0-9A-Fa-f]+) bytes")


def main() -> None:
    body: list[str] = []
    asserts: list[str] = []
    for header in sorted((ROOT / "config/types").glob("*.h")):
        current = None
        last_comment = ""
        for line in header.read_text(encoding="utf-8").splitlines():
            m = STRUCT.match(line)
            if m:
                current = m.group(1)
                size = SIZE.search(last_comment)
                if size:
                    asserts.append(f"_Static_assert(sizeof(struct {current}) == {size.group(1)}, \"{current} size\");")
            elif line.startswith("};"):
                current = None
            if line.strip().startswith("/*"):
                last_comment = line
            elif line.strip():
                last_comment = last_comment if line.strip().startswith(("*", "typedef")) else ""
            if current:
                n = NAMED.match(line)
                if n:
                    asserts.append(f"_Static_assert(offsetof(struct {current}, {n.group('name')}) == {n.group('off')}, "
                                   f"\"{current}.{n.group('name')}\");")
                f = FIELD.match(line)
                if f:
                    line = f"{f.group('indent')}uint32_t {f.group('name')}{f.group('arr') or ''};"
            body.append(line)
    src = "#include <stddef.h>\n" + "\n".join(body) + "\n" + "\n".join(asserts) + "\nint main(void) { return 0; }\n"
    from pipeline import msvc_env  # noqa: E402 (needs the tools dir on sys.path)
    env = msvc_env()
    cl = shutil.which("cl", path=env.get("PATH") or env.get("Path"))
    if not cl:
        sys.exit("[check_types] cl.exe not found in the vcvars64 environment")
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "check_types.c"
        c.write_text(src, encoding="utf-8")
        r = subprocess.run([cl, "/nologo", "/TC", "/std:c11", "/c", str(c), f"/Fo{tmp}\\"], capture_output=True,
                           text=True, env=env, cwd=tmp)
    print(r.stdout.strip())
    if r.returncode != 0:
        sys.exit(f"[check_types] layout errors ({len(asserts)} assertions)")
    print(f"[check_types] {len(asserts)} offset and size assertions hold")


if __name__ == "__main__":
    main()
