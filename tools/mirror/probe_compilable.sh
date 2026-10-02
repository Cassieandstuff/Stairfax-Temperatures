#!/usr/bin/env bash
# Probe which decomp TUs compile 64-bit with the mirror's include order + flags
# (see build_mirror.py), including the implicit-declaration pointer-return gate.
# Prints passing TUs as build_manifest.txt entries; summary to stderr.
set -u
cd "$(dirname "$0")/../.." || exit 1
python3 - <<'EOF'
import sys, subprocess, tempfile
from pathlib import Path
sys.path.insert(0, "tools/mirror")
import build_mirror as bm, passes
bm.gen_headers(passes.load_pointer_rules(bm.MIRROR / "rules"))   # complete mirror/include
ok = fail = 0
with tempfile.TemporaryDirectory() as td:
    for f in sorted(Path("decomp/src/main").rglob("*.c")):
        r = subprocess.run([bm.CC, *bm.CFLAGS, *bm.INCLUDES,
                            str(f), "-o", f"{td}/p.o"], capture_output=True, text=True)
        hz, _ = bm.implicit_decl_hazards(r.stderr)
        if r.returncode == 0 and not hz:
            print(f.relative_to("decomp")); ok += 1
        else:
            fail += 1
print(f"probe: {ok} compile, {fail} fail", file=sys.stderr)
EOF
