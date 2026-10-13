# mirror/rules — the hand-authored transform manifest

These TOML files are the **only** hand-written part of the game-code side of the
port. The generated portable game code (`mirror/src/**`) is downstream of them:
you never edit `mirror/` output by hand — you add a rule here and regenerate with
`tools/mirror/mirror_build.py`.

Everything else under `mirror/` (`mirror/src/`, `mirror/worklists/`) is generated
and gitignored. Only `mirror/rules/` is tracked.

## Why rules and not a fork

The pristine `decomp/` byte-matches the retail DOL and is our correctness oracle.
If we hand-edited a copy of it, that copy would drift the moment a newer decomp
snapshot is vendored, and we'd lose the oracle. Expressing every change as a rule
means re-vendoring is just a re-run: the passes reapply against the same manifest
and report only the rules that no longer match.

This is the same idea as `patches/decomp-msvc-compat.patch`, scaled up from "one
patch" to "a manifest of typed transforms".

## Files

- `pointers.toml` — P2 pointer-width resolutions (promote a narrow global to
  `uintptr_t`, accept/return a real pointer from an allocator, widen a round-trip,
  or mark a cast audited-benign). Driven by the P2 worklists.
- `hardware_stores.toml` — P5 rewrites of direct GX FIFO / MMIO stores into
  portable submit calls (kills the Win32 VEH trap).

## Workflow

1. `python3 tools/mirror/mirror_build.py --worklist decomp/src/main/<tu>.c`
2. Read the worklist. For each **critical** finding, add the matching rule here.
3. Regenerate. Resolved sites drop off the worklist; unresolved criticals keep
   failing the build loudly (that's the point).

## Status

Skeleton. P2 (pointer-width) is the only pass that consumes rules today; the rule
*shapes* below are stubs pending the pass implementations that enforce them
(P3 endian, P4 arch, P5 hardware-store). See
[`docs/adr/0001-native-64bit-portable-mirror.md`](../../docs/adr/0001-native-64bit-portable-mirror.md).
