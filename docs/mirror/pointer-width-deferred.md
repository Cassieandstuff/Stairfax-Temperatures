# Pointer-width findings deliberately left unresolved

Each of these is a real 64-bit truncation that a `mirror/rules/` rule **cannot**
fix soundly on its own. A rule that silenced the scanner here would read "0
critical" while a pointer still truncates somewhere else, which is exactly what
the pipeline exists to prevent. They need a design decision or a layout pass (P3)
first. Regenerate the live list with
`python3 tools/mirror/mirror_build.py --scan-tree decomp/src/main`.

None of these TUs are in the 64-bit build set (`tools/mirror/build_manifest.txt`)
today, and the build set is at 0 criticals.

## Needs an API / design decision

### `audio.c` — the ARQ (ARAM DMA queue) SDK API is 32-bit by contract
- `AudioAramReadCompleteCallback(u32 request)` (237) and
  `AudioAramWriteCompleteCallback(u32 request)` (278) receive the request pointer
  as `u32`, because the SDK's `typedef void (*ARQCallback)(u32 pointerToARQRequest)`
  (`dolphin/ar.h:11`) says so.
- The same calls also pass main-memory buffers as `(u32)buf` / `(u32)addr`
  (`audio.c:131, 270`) into `ARQRequest`'s `u32 source/dest` fields.
- **Why no rule:** retyping the callback params alone would clear the scanner
  while every DMA still truncates its main-memory address. The mirror needs its
  own ARQ seam (a runtime ARQ that carries `uintptr_t` addresses, with
  `ARQRequest`/`ARQCallback` widened together, plus `musyx/runtime/aram_queue.c`
  and `dolphin/ar/arq.c`). Same shape as the OS-globals accessor in ADR 0001.

### `texture.c` — tagged id-or-pointer (`textureIdxToPtr`, 88-92)
- `if ((u32)idx & 0x80000000) return (void*)idx;` A set bit 31 means "this int
  is already a pointer"; otherwise it's a 1-based texture index.
- **Why no rule:** the tag relies on GameCube MEM1 pointers all having bit 31 set
  (0x8xxxxxxx). Host pointers don't, and a 64-bit pointer can't share a 32-bit
  tag space. Options: widen to `uintptr_t` and tag differently (index range
  check, a low tag bit, or a separate handle table). That's a design call for the
  texture system, not a mechanical widen.

## Needs a memory-layout pass (P3)

### `modelEngine.c` — intrusive list with `int` links in object memory (`objListAdd`, 697-720)
- `*(int*)(prev + list->nextOffset) = item;` stores the link **inside the object
  at a byte offset**, as a 4-byte int. Widening the params doesn't widen the slot
  the link lives in.
- Also: the port never host-compiles `modelEngine.c` (it's substituted by
  `bridge/game_model_support.c`; see `docs/PORT_ARCHITECTURE.md`).

### `shader.c` — romlist pointer stored into an `int` table (`mapProcessRomList`, 2987-3040)
- `rl = (int)mapGetRomListAndOffsets(slot, 0);` then
  `*(int*)(slot * 4 + 0x83A8 + base) = rl;` writes it into a 4-byte-slot table at
  a fixed offset inside a structure, and `(int*)(base + 0x417C)` reads another
  pointer the same way.
- The scanner used to blame the index param `slot` for this. It's now refined to
  skip a scaled-index param, and the real hazard (`int rl`) is caught by
  `local_int_holds_ptr`. The fix is the table's slot width and offsets, i.e.
  layout, not the local.

## Newly visible, not yet triaged
The `local_int_holds_ptr` detector (function-local ints that carry a pointer)
surfaced hazards outside this pass's scope. Most look like straightforward
retype + widen_cast fixes, the same pattern as `objprint.c`/`shader_dolphin.c`:
`mm.c:963` (freePtr), `model.c:52`, `newshadows.c:1345`, `object.c:2430`,
`objhits.c:1816, 1884`, `objlib.c:627, 701`, `pi_dolphin.c:4552, 4579, 4730`,
`shader.c:827`. `objlib.c:701` writes links at `OBJLINK_CHILD_LIST_OFFSET`, so
check it for the same layout problem as `modelEngine.c` before adding a rule.

## Scanner blind spots found while doing this
- **Pointer-size strides:** `walk += 4` stepping a pointer array (`objprint.c`
  objRender) isn't detected. It's fixed by `[[pointer.stride]]`, and the
  `objrender_staff` difftest proves it's load-bearing (without it the mirror
  segfaults). Other `+= 4` array walks will only be found by reading code or by
  difftests.
- **Values crossing calls:** a pointer cast to `int` at a call site (e.g.
  `gametext.c:172`) is only found by reading callers.
