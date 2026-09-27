# tools/mirror — decomp → portable-mirror transform pipeline

Turns the frozen `decomp/` (byte-matching oracle) into a portable, native-64-bit
`mirror/` (the ship build target). Architecture and rationale live in
[`docs/adr/0001-native-64bit-portable-mirror.md`](../../docs/adr/0001-native-64bit-portable-mirror.md).

## Layout

```
tools/mirror/
  mirror_build.py   orchestrator + CLI
  passes.py         the passes (P0..P6); P2 pointer-width is implemented
mirror/
  rules/            hand-authored transform manifest (TRACKED)
  src/              generated portable C           (gitignored)
  worklists/        generated P2 sidecar JSON       (gitignored)
```

## Passes

| Pass | Does | Status |
|------|------|--------|
| P0 | vendor + apply the msvc-compat baseline patch | identity copy (patch hook TODO) |
| P1 | parse (libclang if importable, else heuristic) | fallback active |
| P2 | pointer-width: worklist (find) **and** rewriter (fix, rule-driven) | **implemented** |
| P3 | endian: annotate on-disc structs for `beFix*` | TODO |
| P4 | arch intrinsics → portable / HAL | TODO |
| P5 | rewrite `0xCC008000` FIFO stores → submit call | TODO |
| P6 | emit formatted C + provenance header | identity write-through |

## Usage

```sh
# P2 worklist for the collision pilot TU (stdout):
python3 tools/mirror/mirror_build.py --worklist decomp/src/main/track_dolphin.c

# ...written to a tracked snapshot:
python3 tools/mirror/mirror_build.py --worklist decomp/src/main/track_dolphin.c \
        --out docs/mirror/worklist-track_dolphin.md

# aggregate P2 scan across a whole tree (per-TU ranking + category totals):
python3 tools/mirror/mirror_build.py --scan-tree decomp/src/main \
        --out docs/mirror/scan-decomp-main.md

# emit the portable mirror TU: applies the mirror/rules/ pointer rules (P2 rewrite)
# and reports critical findings before -> after:
python3 tools/mirror/mirror_build.py --emit decomp/src/main/track_dolphin.c
```

## The P2 rewriter

`--emit` reads `mirror/rules/pointers.toml`, applies the pointer-width rules, and
writes the transformed TU to `mirror/src/`. It is **idempotent** (re-emitting
yields byte-identical output) and **rule-driven** (it changes nothing the manifest
does not name). Rule kinds and their known limits are documented in
`mirror/rules/pointers.toml`. The loop:

1. `--worklist <tu>` → read the criticals.
2. Add a `promote` / `ret` / `widen` / `audit` rule per critical to `pointers.toml`.
3. `--emit <tu>` → see `critical … N -> M`; iterate until 0.

A "0 critical" emit clears every *line-visible* hazard; it is not a proof of
correctness. Prototype pointer-params typed `int` and cross-call pointer flow are
not line-visible — closing those is type-aware (libclang) work, and the ultimate
gate is the oracle-vs-mirror behavioral diff (ADR 0001).

Requires only Python 3.11+ (stdlib). If the `clang.cindex` bindings are present,
P1 uses libclang; otherwise it falls back to the heuristic line scanner, which is
what the skeleton P2 uses today. No third-party packages.
