# ADR 0001 — Native 64-bit, platform-agnostic port via a transformed decomp mirror

- **Status:** Accepted (foundational; supersedes the 32-bit-only assumptions in the current build)
- **Date:** 2026-09-27
- **Deciders:** project owner
- **Context doc:** [`PORT_ARCHITECTURE.md`](../PORT_ARCHITECTURE.md)

## TL;DR

We are pivoting the ship target from **Win32 / MSVC / 32-bit** to **native 64-bit and
platform-agnostic** (Windows, Linux, Android to start). We keep the pristine vendored decomp
(`decomp/`) as a **correctness oracle** and build from a **mechanically transformed portable mirror**
(`mirror/`, generated — never hand-edited). The pointer model is **native 64-bit host pointers with
load-time relocation** (not a guest address space). No copyrighted asset ever enters the repo or the
binary: assets are supplied by the user at **runtime** from their own retail dump.

## Context

The current port is fast to bring up precisely because of one trick (Key invariant #1 in
`CLAUDE.md`): **MEM1 is mapped at the real `0x80000000`, so absolute guest pointers just work.** That
requires:

- a **32-bit** host `void*` (Gekko is 32-bit PowerPC; every pointer in the game's structs, on-disc
  data, save data, and relocation tables is 32 bits),
- **Win32-only** mechanisms — `LARGEADDRESSAWARE`, a vectored exception handler (VEH) for the WGPIPE
  MMIO trap,
- the **MSVC 32-bit** toolchain (`build.bat` → `vcvarsall x64_x86`).

This is a bring-up accelerator with a hard ceiling. A shipping product that people mod (HD textures,
replacement models, custom shaders) and that runs on Linux/Android/Steam Deck cannot be locked to
32-bit Win32. This is the "Skyrim Special Edition" problem: the 32-bit engine is the thing you
eventually have to leave behind, and the cost of leaving grows with every 32-bit assumption you build
on top of it. **The project is early — this is the cheapest moment to pivot.**

Two structural facts make the pivot tractable now:

1. The **RHI already has Vulkan/D3D/GL backends.** Vulkan alone covers Windows + Linux + Android.
2. We already committed to **byte-neutrality** (load-time `beFix*` swaps). Endianness is half of what
   makes a struct portable; that groundwork carries straight over.

## Decision

### 1. Native 64-bit, platform-agnostic ship target

Host `void*` is 64-bit. Target Windows + Linux + Android first (all via the Vulkan RHI backend). The
Win32 platform layer (`plat_window_win32.c`) is replaced by a portable window/input layer (SDL3).

### 2. Decomp is the oracle; a transformed mirror is the build target

```
decomp/   (vendored, FROZEN, byte-matches the retail DOL)   ── verification oracle
   │
   ▼   transform pipeline (scripted, deterministic, idempotent)
mirror/   (GENERATED, portable, 64-bit clean)                ── the ship build target
   │
src/      (hand-written port: HAL, bridge, platform)         ── links against mirror/
```

- **`decomp/`** stays exactly as it is: vendored, compiled only for verification, governed by its own
  inner `CLAUDE.md` (byte-matching rules). We do **not** hand-edit it. (Vendored copy for now; can
  become a submodule later without changing anything downstream.)
- **`src/`** is reserved for hand-written **port** code (HAL / bridge / platform). No game logic.
- **`mirror/`** is **generated output**, gitignored like any build artifact. It is never edited by
  hand — every change to game code is expressed as a **transform rule** applied to `decomp/`.

**Why a mechanical mirror and not a hand-fork:** a hand-edited copy drifts from the decomp the moment
a newer decomp snapshot is vendored, and you lose the oracle. A scripted transform re-applies on
re-vendor. This is the same pattern the repo already uses at small scale with
`patches/decomp-msvc-compat.patch` — this ADR scales that one idea up into a pipeline.

### 3. Pointer model: native pointers + load-time relocation

Guest pointers become **real 64-bit host pointers**. Anything that arrives from disc/save with an
embedded 32-bit pointer is **widened and relocated at load time**, on the same seam as the existing
endian fixups. We explicitly reject the alternative (a `rdram`-style guest address space with
per-access translation): it is closer to the emulator model the project rejected, and it taxes every
memory access forever. See "Alternatives".

### 4. No copyrighted data in the repo or the binary

The engine binary contains **zero** game assets. The user supplies their own retail dump at
**runtime** (the SoH / AM2R model). This is both the correct legal posture and the thing that
incidentally lets the cloud container and CI compile the code (code-only build, assets not required to
link). See "Assets & legal".

## The transform pipeline (sketch)

Goal: turn `decomp/` C into `mirror/` C that a modern 64-bit clang/gcc/MSVC compiles and that runs
identically to the original. The pipeline is a sequence of **passes**, each deterministic and
idempotent, driven by a manifest of rules so re-vendoring is a re-run.

```
decomp/**.c,h ─┬─► [P0 vendor+patch] ─► [P1 parse] ─► [P2 pointer-width] ─► [P3 endian] ─►
               │      apply msvc/           libclang      widen guest ptrs      annotate on-disc
               │      compat patch          AST            in serialized        structs for
               │                                            structs; relocate    beFix* at load
               │
               └─► [P4 arch-intrinsics] ─► [P5 hardware-store] ─► [P6 emit] ─► mirror/**.c,h
                      PPC paired-single/       rewrite 0xCC008000    formatted C,
                      MSR/HID0 → portable      FIFO stores → direct  provenance header
                      or HAL calls             submit call (no VEH)  (source + rule ids)
```

**P0 — vendor + baseline patch.** Copy `decomp/` verbatim, apply the existing MWCC→MSVC compat patch
(`AT_ADDRESS`, the one `void*`→`LightmapVertex*` cast). Nothing creative here; it is the current
`patches/` step, made the first pipeline stage.

**P1 — parse.** Build an AST per TU (libclang). Everything below operates on the AST + a rules
manifest, not on text regexes, so the transforms are robust to formatting.

**P2 — pointer-width.** The heart of the pivot. Classify every pointer:
- *Runtime-only pointers* (never serialized): leave as native `void*` — 64-bit is automatic.
- *Serialized / on-disc / save / relocation-table pointers*: these are the danger. In the original
  they are 32-bit slots inside structs. Two sub-rules:
  - the **struct field** stays a real pointer type (so host code dereferences it natively), but
  - the **loader** for that struct is marked to **widen + relocate** the 32-bit on-disc value into a
    64-bit host pointer at load time (see P3's seam).
- *`u32`↔pointer round-trips* (very common in this codebase, e.g. `gIntersectLinePool` stored as
  `int`): flagged as errors for a human rule decision — either promote the storage to `uintptr_t`, or
  route through a handle. This is the bulk of the "massaging" and the pass emits a **worklist** rather
  than guessing.

**P3 — endian.** We already do this by hand via `beFix*`. This pass *annotates* which structs are
loaded from big-endian disc data so the load-time fixup (now also the pointer-relocation site) is
generated consistently instead of remembered. Byte-neutrality was the right early call precisely
because it feeds this pass.

**P4 — arch intrinsics.** Replace PPC-specific constructs with portable equivalents or HAL calls:
paired-single matrix ops (`ps_muls0/ps_madds1/...`) → scalar float or SIMD in a HAL math TU;
`MSR/HID0` supervisor pokes → no-ops (no host equivalent, already the scaffold behavior). Much of this
already exists in `src/hal` and `game_boot_stubs.c` — the pass routes to it rather than reinventing.

**P5 — hardware-store rewrite.** The non-C++-clean render TUs submit geometry via direct stores to
`GXWGFifo` (`0xCC008000`), caught today by a Win32 VEH trap. Because we are *allowed to transform the
mirror*, this pass rewrites those stores into a **direct FIFO-submit call**. The VEH trap disappears —
which is mandatory anyway, since VEH does not exist on Linux/Android.

**P6 — emit.** Write formatted C into `mirror/`, each file carrying a provenance header (source path,
decomp revision, list of rule ids applied) so a diff in behavior can be traced back to a rule.

**Verification (the oracle in action).** Two levels:
- *Structural:* CI re-runs the pipeline and asserts `mirror/` is byte-identical to a re-run (the
  transform is deterministic); any un-handled P2 worklist item fails the build loudly.
- *Behavioral:* keep the **32-bit oracle build** alive as a test target. For subsystems with pure,
  deterministic cores (collision queries, the anim matrix decode, math), run the same inputs through
  the oracle (real decomp) and the mirror and assert identical outputs. This is how you *prove* a
  transform preserved behavior instead of hoping.

### Rules manifest

Each pass reads `mirror/rules/*.toml` (owned, hand-written, small). A rule is e.g. "field
`MapBlock.gcPolygons` is a serialized pointer → relocate at load in `map_block_load`" or "`0xCC008000`
store → `gx_fifo_submit_u32`". The manifest *is* the human-authored part of the game-code changes; the
mirror is downstream of it. Re-vendoring a newer decomp re-runs the passes against the same manifest
and reports only the rules that no longer apply.

## Assets & legal

**Never host or embed copyrighted data.** Not the ISO, not the DOL, not extracted `.inc` assets.
The current `.gitignore` already excludes `*.iso`, `*.dol`, `*.inc`, `orig/` — keep that.

Three ways assets reach a build, and the decision among them:

| Path | How | Verdict |
|------|-----|---------|
| Embed extracted `.inc` into the binary (today: `boot_logo.c`) | Compiled into the exe | **Retire.** Puts copyrighted data in the binary; blocks CI/cloud linking; illegal to distribute. |
| CI pulls assets from an encrypted secret | GH Actions secret holds the dump | **No.** A full ISO in a secret is impractical and legally dicey. |
| **Runtime asset loading from a user-supplied dump** | Binary ships asset-free; user points `STAIRFAX_ISO` at their own retail dump at launch | **Yes.** The SoH/AM2R model. Legal, and it makes the code-only build compile without assets. |

Consequences of choosing runtime loading:
- The binary is **asset-free** → the repo can be public later without legal exposure. Keeping it
  private during development is fine but is no longer *required* by the asset question.
- The cloud container / CI can **compile and link** the mirror target (no assets needed to build) —
  which quietly resolves the "cloud can't build" problem from the 32-bit era. Assets are only needed
  to *run*, which happens on a machine that has the user's dump.
- "Pull assets from my local machine while building in the cloud" is **not needed** and not possible
  (the cloud container can't reach your filesystem) — but it doesn't matter, because building never
  needs assets under this model. Running does, and running happens locally where the dump lives.
- Migration task: move `boot_logo.c`'s embedded texture (and the `gLoadingScreenTextures.inc`
  include path) from compile-time embed to a runtime load from the user's dump.

## Consequences

**Positive**
- Ships 64-bit on Windows/Linux/Android; unlocks mod support (the whole point).
- The mirror target builds with clang/gcc/MSVC — **the cloud container and CI can finally build it.**
- The VEH trap and other Win32-isms are deleted, not ported.
- The decomp stays a pristine, re-vendorable oracle; behavior is *provable*, not hoped.
- Byte-neutrality and the RHI's multi-backend design pay off.

**Negative / costs**
- Defers gameplay progress (e.g. collision bring-up) in favor of foundation work.
- The transform pipeline is real engineering (libclang passes + a rules manifest + a diff harness).
- P2's `u32`↔pointer worklist is genuine per-site labor; there is no fully-automatic answer.
- We must maintain the 32-bit oracle build as a test target for behavioral diffing.

**Neutral**
- The HAL/bridge/scaffold taxonomy survives; only the memory floor of the HAL changes.
- Gameplay *logic* ports identically — only the pointer/memory substrate under it changes. Work like
  collision bring-up is not wasted; it should be the **first subsystem brought up through the mirror**,
  because it exercises the whole pipeline (pointer widening, endian, no-VEH FIFO) end to end.

## Alternatives considered

- **Stay 32-bit Win32.** Rejected: hard ceiling for a moddable, cross-platform release; only gets more
  expensive to leave.
- **Guest address space + per-access translation (`rdram` + `MEM_*` macros, N64Recomp style).** Less
  per-struct massaging, but a permanent per-access tax and a step back toward the emulator model this
  project rejected. Native-pointer + relocation preserves the "port, not emulator" spirit.
- **Hand-forked portable copy of the decomp.** Rejected: drifts from the oracle on every re-vendor;
  the whole value of the decomp is that it stays a verifiable reference.

## First steps

1. Scaffold the pipeline: `mirror/rules/` manifest format + a P0/P1 skeleton (vendor + parse) that
   emits an unchanged `mirror/` and a P2 pointer worklist for one TU.
2. Pick `track_dolphin.c` (collision) as the pilot subsystem — self-contained, meaty, and it forces
   every pass to exist.
3. Stand up the behavioral diff harness (oracle vs mirror) on collision queries.
4. Migrate `boot_logo.c` to runtime asset loading to make the mirror target link asset-free.
