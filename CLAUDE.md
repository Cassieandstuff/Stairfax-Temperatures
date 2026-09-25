# Stairfax Temperatures — Star Fox Adventures PC Port

A native PC port of Star Fox Adventures (GameCube). It is **"a port, not an emulator"**:
`game_engine.exe` runs the game's own recompiled C — the real `main()`/`init()`/`gameLoop()`
and hundreds of real TUs — against a host HAL that reimplements only the hardware (VI/GX/OS/DVD/MM)
and a Vulkan/D3D/GL renderer. The CPU runs the game's actual functions on x86.

## Repository shape
- **Port code (this repo root):** `src/`, `include/`, `docs/`, `CMakeLists.txt`, `build.bat`.
- **`decomp/`:** the **vendored** SFA decompilation (frozen ~98.6% match), compiled unmodified.
  `GAME_ROOT`/`DECOMP_ROOT` points here. Treat it as a read-only dependency — see below.
- Build: **`build.bat`** (configures + builds `game_engine` via Ninja/MSVC 32-bit). The build
  must reach `Linking game_engine.exe` with **EXIT 0** before any commit.

## The vendored decomp is (almost) untouchable
- `decomp/**` is compiled **as-is**. Do NOT hand-edit decomp sources to work around a port
  problem — fix it in a port HAL/bridge file instead. The port's whole value is that it runs
  the real code unmodified.
- The **only** sanctioned decomp edits are MWCC→MSVC portability shims, already applied and
  captured in `patches/decomp-msvc-compat.patch`: `GXWGFifo AT_ADDRESS(0xCC008000)` (the
  `AT_ADDRESS` macro in `decomp/include/dolphin/types.h` expands to `: (addr)` under MWCC and
  to nothing under MSVC) and one explicit `void*`→`LightmapVertex*` cast. If you ever re-vendor
  a newer decomp snapshot, re-apply that patch and nothing else.
- The decomp's own `CLAUDE.md` (inside `decomp/`) governs decomp byte-matching only. Its rules
  (banned constructs, pool reconstruction, saved-register coloring, `report.json` fuzzy match)
  are IRRELEVANT to port code and do not apply here.

## Port code taxonomy (4 roles, enforced by directory + CMake)
See `docs/PORT_ARCHITECTURE.md` for the full contract. In short:
- **`src/hal/`** — PERMANENT reimplementation of GC hardware/SDK (VI, GX/FIFO, OS, MM, DVD, the
  WGPIPE MMIO trap, the RHI renderer backends). This is real, keep-forever port code.
- **`src/bridge/`** — PERMANENT port-native glue between real game code and the HAL (resource/DLL
  registry, scene render, save-state, asset loaders, endianness fixups, the uiDLL loader spine,
  the loading-screen bring-up).
- **`src/scaffold/`** — TEMPORARY stubs standing in for not-yet-ported engine symbols. Tracked in
  `docs/SCAFFOLD_MANIFEST.md`. **Bring-up discipline:** when a real TU lands, add it to a CMake
  `GAME_*` group, delete the scaffold it replaces, and remove its manifest row.
- **`src/platform/`** — PERMANENT host OS window / raw-input layer.

## Key invariants (hard-won; violating these breaks the port)
- **MEM1 is mapped at real `0x80000000`** (LARGEADDRESSAWARE). Absolute guest pointers just work.
- **On-disc / embedded game data is big-endian.** Real game TUs reading it need a load-time
  byte-swap to host order, per struct layout (`include/port/byteswap.h`, `beFix*`). This is the
  single most common source of "garbage data" bugs.
- **Non-C++-clean C render TUs** submit geometry via direct stores to `GXWGFifo` (`0xCC008000`).
  A VEH trap (`src/hal/wgpipe_trap.c`, auto-armed at CRT init) decodes those and routes them to
  the software FIFO. Don't "fix" the store — it's intentional.
- **GC→Vulkan clip space:** the game's orthographic projections (`C_MTXOrtho`) are GC-convention
  (z ∈ [-1,0]); `GXSetProjection` remaps `GX_ORTHOGRAPHIC` to the RHI's [0,1] range. Perspective
  projections are built RHI-convention in the bridge and left alone.
- **GC texture endianness** is handled inside `tex_decode.c` per format (e.g. IA8 packs alpha
  high, intensity low) — not by the caller.

## Working agreements
- **Commit only when asked.** Never commit the copyrighted extracted assets (`*.inc`, gitignored)
  or anything under `orig/` (the user's DOL/ISO).
- Edit SJIS-bearing files byte-wise (python `rb`/`wb`), never with a text editor that reencodes.
- Never bare `git stash` in a worktree — use `git checkout -- <file>` or a temp WIP commit.
- `NEVER` write comments in decomp sources. Port files are documented deliberately; match the
  surrounding density.

## Run
`game_engine.exe` needs `STAIRFAX_ISO` set to your retail ISO. Useful env: `STAIRFAX_CAP=path[,frame]`
(desert-scene capture), `STAIRFAX_LOADING_HOLD=N` + `STAIRFAX_LOADING_CAP` (watch/capture the boot
logos), `STAIRFAX_PLAYER_DLL`, `STAIRFAX_FORCE_STICK*` (test infra). See `docs/` for subsystem notes.
