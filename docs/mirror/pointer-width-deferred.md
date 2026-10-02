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

### `pi_dolphin.c` — the MLDF resource table (`gResourceFileBuffers` & co.)
Investigated as its own pass; **not resolvable with pointer-width rules**, and the
port's architecture already says why.
- **Two views of one block.** On the console, `gResourceFileBuffers` /
  `gResourceFileSizes` / `gMapRomListBuffers` / `gResourcePendingMapIds` are the
  `ptrs` / `sizes` / `romList` / `ids` arrays *inside* `struct MldfTables`, one
  0x20000-byte block at 0x80345E10. The loader reaches it both through those
  symbols (52 uses of the buffer table) and through
  `struct MldfTables* tbl = (struct MldfTables*)gResourceFileTable` (94 `tbl->`
  uses, 225 `MLDF_*` macro uses, 6 functions).
- **The second view is already broken on any host.** The decomp defines
  `u8 gResourceFileTable[0x160]` and the arrays as separate globals, so `tbl->ptrs`
  reads past a 0x160-byte array and never sees `gResourceFileBuffers`.
- **The struct view is addressed with GameCube layout literals.** 29 MWCC-shaped
  biased slot addresses (`(slot << 2) + ((u32)&tbl->ptrs[0] + 0x6A28)`, `+0x6D68`,
  `+0x6C08`), 5 `himem - 27176`-style computations, `+ 0x80000000` displacements,
  and 4-byte slot strides. On 64-bit, `DVDFileInfo* fileInfo[0x58]` alone grows
  0x160 bytes and moves every later array, so every literal goes stale.
- **The port replaces this loader rather than compiling it.**
  `bridge/game_assetfile.c` is the MLDF resident-file table and
  `bridge/game_model_support.c` serves `loadAndDecompressDataFile` (see
  `docs/PORT_ARCHITECTURE.md`). `pi_dolphin.c` isn't compiled by the 32-bit build
  (`scaffold/game_boot_externs.c` stands in for its globals) or the mirror build set.
- **Recommended direction:** the mirror follows the same architecture. A portable
  bridge owns the table with `void*` entries, the decomp's MLDF loader functions are
  excluded from the mirror, and readers like `tex1GetFrame` / the `tab0`/`t25`-style
  lookups (`pi_dolphin.c:4687-5131`) are served by the bridge or brought in only
  after it exists. A rules pass here would be ~300 rewrites of layout-specific
  address math to reach code the port doesn't run.

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

## `local_int_holds_ptr` triage (function-local ints carrying pointers)

**Fixed with rules** (each pointer only ever lives in the local, the widen is
complete): `mm.c:963` freePtr (`mm.toml`, proven by difftest `mm_region` incl. the
`& ~0x1f` alignment branch), `model.c:52` v (inside the `LOADCOLOR_BLOCK` macro),
`newshadows.c:1345` texelAddress, `objhits.c:1816/1850` ob and `1884`
prevSpheres, `objlib.c:627` disguised, `pi_dolphin.c:4552/4579` srcBuf/bounceBuf,
`shader.c:827` cur/end/objStart (the ObjPlacement walk; its `size*4` steps are
on-disc word counts, not pointer strides).

**Deferred:**
- **`object.c` `Obj_UpdateAllObjects` (2424+), `child`:** the local is fixable, but
  the same function walks its object lists with `obj = *(int*)(obj + off)` (int
  links stored *inside* objects, 2452-2497), and the `void (*cb)(int)` hitDetect
  callback is shared between the child and the list walk. Same intrusive-list
  layout problem as `modelEngine.c` `objListAdd` (P3); widening `child` alone would
  clear the finding while every link still truncates.
- **`objlib.c:701` `ObjLink_DetachChild`, `dst`:** shifts the child-pointer array
  inside `GameObject` at the hardcoded byte offset `OBJLINK_CHILD_LIST_OFFSET`
  with a 4-byte stride and `int` copies. Fixed GameCube layout (P3).
- **`pi_dolphin.c:4730` `tex1GetFrame`, `e`:** the cursors are
  `base + offset` with `u32 base = gResourceFileBuffers[idx]`. See the MLDF
  resource-table entry above: it belongs to the bridge-owned table, not a rule.
- **`shader.c:3001` `rl`:** see above (int table at a fixed struct offset).

## Scanner blind spots found while doing this
- **Pointer-size strides:** `walk += 4` stepping a pointer array (`objprint.c`
  objRender) isn't detected. It's fixed by `[[pointer.stride]]`, and the
  `objrender_staff` difftest proves it's load-bearing (without it the mirror
  segfaults). Other `+= 4` array walks will only be found by reading code or by
  difftests.
- **Values crossing calls:** a pointer cast to `int` at a call site (e.g.
  `gametext.c:172`) is only found by reading callers.
