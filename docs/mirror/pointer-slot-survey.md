# Pointer-slot survey: widen, handle, or translate?

Status: survey + recommendation (2026-10). Input to the mirror's memory/pointer model
([ADR 0001](../adr/0001-native-64bit-portable-mirror.md)). Companion to
[`pointer-width-deferred.md`](pointer-width-deferred.md), which tracks individual
findings.

**Question.** On the GameCube a pointer is 4 bytes; on the 64-bit mirror it is 8.
For every place the engine *stores* a pointer, what should the mirror do? The
specific idea under test: replace raw pointers with stable IDs / handles (an entity
manager) wherever that's the better fit.

**Method.** Every pointer-holding slot in the core structures was classified: the
`GameObject` struct and object lists, the MLDF resource table, models, textures,
map blocks/romlists and save data. For each slot: what it points to, who writes and
reads it, whether its byte layout is fixed (on-disc format, `STATIC_ASSERT`ed
offsets, hardcoded `*(int*)(x + 0xNN)` access) and whether it's persisted. Key
claims were spot-checked against the source; counts are greps over `decomp/src`.

---

## TL;DR

1. **Handles are the right tool for a minority of slots, and the engine already
   uses them there.** Saves store objects by `objectId`, never by pointer
   (`SaveGameObjectPosition { u32 objectId; f32 x, y, z; }`). Files are addressed by
   `fileId`, textures by a 1-based index, object types by `romDefNo`. The original
   developers used IDs exactly where data had to persist or be shared. The mirror
   should keep and extend that, not invent a new layer.
2. **Most runtime pointers should just widen.** No `GameObject` field is saved or
   loaded from disc. Parent/child/target/owner links are runtime-only, so native
   64-bit pointers are correct and fastest.
3. **The hard cases aren't struct fields:**
   - **(a) On-disc headers relocated in place:** the model, map-block and romlist
     headers have their `u32` offsets overwritten with absolute pointers. Those
     slots can't widen because the disc layout is fixed, and handles don't fit
     either. They need **load-time translation into host structs**.
   - **(b) The `int obj` convention:** about 2,100 `(GameObject*)` casts and 212
     `(int)obj` casts (`player.c` alone: 646). Objects are routinely passed as plain
     `int`. That's the single largest pointer-width cost in the whole port, and no
     entity manager removes it.
4. **Recommendation:** a five-way strategy (below). Use an entity table only as an
   *optional* safety layer for long-lived cross-object references, not as the
   engine's object model. Decide the `int obj` question first, because it dominates
   cost.

---

## The five strategies

| Strategy | What | Use when | Example |
|---|---|---|---|
| **WIDEN** | Native pointer, 64-bit storage (`uintptr_t` / real pointer type) | Runtime-only slot, layout not fixed by disc/save | `GameObject.anim.parent`, `ObjModel` buffers, `render.c` packed addresses |
| **ACCESSOR** | Absolute hardware/OS address → runtime call | Fixed GC addresses | `*(u32*)0x800000F8` → `os_globals_read_u32` |
| **HANDLE** | 32-bit index/ID into a table, resolved at use | Persisted, shared, overloaded id-or-pointer, or a pointer stuck in a fixed 4-byte slot | texture refs, MLDF `fileId`, save `objectId` |
| **HOST-STRUCT** | Keep the disc bytes as data; build a separate host struct with native pointers at load time (or keep offsets + resolve through an accessor) | On-disc headers whose offsets get relocated in place | `ModelFileHeader`, `MapBlockData`, `MapRomListPage` |
| **LOW-4GB** | Allocate a pool below 4 GB so 32-bit integers holding its pointers stay lossless | Escape hatch for code too entangled to convert | the `int obj` convention, if chosen |

LOW-4GB is what the difftest oracle already does (`mmap MAP_32BIT`). It works on
Linux and Windows x86-64. It is **not portable**: macOS reserves the entire low
4 GB (`__PAGEZERO`), and Android/arm64 has no guaranteed low-address mapping. It
contradicts the ADR's platform goals if relied on permanently, so treat it as a
bring-up crutch with an exit plan, never as the model.

---

## Findings by area

### 1. Game objects (`GameObject`, object lists)

`GameObject` (0x10C bytes) = `ObjAnimComponent anim` (0x00–0xAF) + a tail.
**Every offset is pinned by `STATIC_ASSERT`** (`objanim_internal.h:811-850`,
`object.h:101-104,147-156`), and those asserts must be dropped or made
GC-build-only for the 64-bit mirror.

| Slot (offset) | Points to | Uses | Raw-offset access? | Strategy |
|---|---|---|---|---|
| `anim.parent` (0x30) + `u32 parentAddress` union view | parent object | ~297 | union view is a 4-byte alias | WIDEN, retype the `u32` view |
| `anim.next` (0x38) | next in `gObjUpdateList` | list code | **yes**: `*(int*)(obj + nextOffset)` | WIDEN `ObjLinkedList` (`int head` → `intptr_t`) and `objListAdd/remove/walk` |
| `anim.placementData` (0x4C) + `u32 placementDataAddress` | on-disc placement record | ~585 | union view | WIDEN (only the pointee is disc data) |
| `anim.modelInstance`, `hitReactState`, `modelState`, `dll`, joint/texture/hit-volume buffers, `banks` | runtime buffers / DLL interface | tens to ~440 each | no | WIDEN |
| `anim.targetObj` (0xA4) | target object (AI/camera) | ~437 | no | WIDEN; **optional HANDLE** (dangling-prone) |
| `extra` (0xB8) | per-class state block | ~2,158 | `*(int*)&obj->extra` null-test launders | WIDEN, fix the launders |
| `ownerObj` (0xC4), `pendingParentObj` (0xC0) | other objects | ~87 / ~49 | some classes reuse `ownerObj` as f32 scratch | WIDEN; **optional HANDLE**; audit the f32 reuse |
| `childObjs[5]` (0xC8) | child objects | ~138 | **yes**: `OBJLINK_CHILD_LIST_OFFSET` + `slot += 4` + `*(int*)` copies (objlib.c:704-714, 120); object.c colour-fade `childScan += 4`; objprint `walk += 4` (fixed via `[[pointer.stride]]`) | WIDEN; rewrite the stride code to index `childObjs[]` |
| `userData1/2` (0xF4/0xF8, `s32`) | per-class scratch, sometimes an object | n/a | n/a | per-class audit; HANDLE where it holds an object |
| callbacks, `msgQueue` | functions / queue | few | no | WIDEN |

Global lists: `gObjList` / `gObjDeferredFreeList` (`GameObject**`) and
`gObjectTypeList` widen cleanly. **Exception:** `gObjList` removal shifts entries with
byte arithmetic assuming a 4-byte stride (object.c:1460-1473), which needs a fix.

**Identity today.** The only stable instance ID is the placement `objectId`
(on-disc, read via `anim.placementData`). `ObjList_FindObjectById` (object.c:2208) is
a linear scan of `gObjList`, used from ~30 files. Spawned objects without a placement
have **no** stable ID. There's no generation counter, so a stale reference can't be
detected.

**None of these object references are persisted or loaded from disc.** Nothing
forces a 32-bit layout on them.

### 2. Resource files (MLDF table)

- `fileId` (0..0x57) **already is the handle**. Every consumer outside the loader
  goes through `fileId`-keyed getters and keeps the returned pointer in a local
  (callers of `loadAndDecompressDataFile` in model/texture/tex_dolphin/voxmaps/engine
  DLL 2, `getDataFileSize` used purely as a size). Nothing stores a table pointer
  into a fixed slot.
- The table itself (`ptrs` / `romList` / `fileInfo`) is loader-private and is
  WIDEN-in-a-port-native-table. The port already replaces the loader
  (`bridge/game_assetfile.c`, `bridge/game_model_support.c`); see
  `pointer-width-deferred.md`. Fix the `extern u32 gResourceFileBuffers[]`
  declaration (`pi_dolphin_api.h:54`) when that table moves.

### 3. Models

- **`ModelFileHeader` is relocated in place.** `ObjModel_RelocateModelData`
  (model.c:2567) rewrites ~25 `u32` disc offsets into absolute pointers
  (`jointData = m + *(u32*)&jointData`, vertices/normals/colors/texCoords,
  display lists, render ops, collision, anim tables…). It also rewrites nested
  records: display-list entries (0x1C stride), `morphTargetPtrs[]`, and vertex-anim
  chunks (0x74 stride, `weightStream`). The counts after 0xD8 sit at disc offsets,
  so **widening is impossible: HOST-STRUCT.** Either keep the blob and build a
  `ModelHeaderHost` with native pointers (same field names) at load, or keep the
  offsets and resolve through `MODEL_PTR(m, field)`-style accessors.
- `ObjModel` (runtime-allocated, ~20 pointers): WIDEN.
- `blendAnimData[i] = *(int*)&dst->normalBuf + off` (model.c:2553) stores an
  address in an `s32` array: WIDEN or store just the offset.
- `ModelList` / `gModelAnimCacheList` entries are `{s16 id; bytes[sizeof(u8*)]}`
  accessed via `memcpy`, so they're already width-agnostic. The id is the handle.

### 4. Textures

- **Texture references are already handles almost everywhere.** `ShaderLayer`'s
  `{ s32 textureIndex; Texture* texture; }` union, `Shader` aux/indirect/texture
  ids, and `ModelFileHeader.textureIds` all hold **ids**. `ObjModel_ResolveRenderOpTextures`
  (model.c:2464) maps file-local indices to global ids, and readers resolve through
  `textureIdxToPtr` (26 call sites). HANDLE: keep it that way and make the union
  id-only.
- **`textureIdxToPtr`'s bit-31 passthrough** (`if (idx & 0x80000000) return (void*)idx;`)
  assumes every MEM1 pointer has bit 31 set. That's false on a host. It must go:
  find which callers pass a real pointer and give them a separate path, or allocate
  ids for those textures too.
- `MapBlockData.textures` (0x54) converts on-disc ids to pointers **in place**.
  HANDLE: keep the id, resolve at use (`tex_dolphin.c` ~1496).
- Runtime `Texture` fields and `gLoadedTextures[]`: WIDEN (relax the offset asserts).

### 5. Map blocks and romlists

- **`MapBlockData`** is relocated in place by `MapBlock_init` (tex_dolphin.c:1510):
  `gcPolygons`, `polygonGroups`, `vertices`, colours/texcoords, render instruction
  streams, `displayLists` (+ nested `dlist`), `shaders`, `textures`. It also gets
  runtime pointers written into disc-header slots (`hits` 0x70, `auxData` 0x74).
  The u16 counts at 0x84-0xA2 pin the layout: **HOST-STRUCT.**
- **`MapRomListPage`** (0x38, asserted) header slots `cells` / `objects` /
  `layerRects` / `loadedObjectBits` are *computed* by code as `(int)page + offset`
  (shader.c:2523, 3078-3087). Because code writes them, a host page header can
  WIDEN them; keep the disc bytes separate.
- **Romlist tables at `base + 0x83A8` (int[80]) and `base + 0x418C`
  (`ShaderRomListSlot`, hardcoded 8-byte stride):** fixed byte offsets into a
  shared block, ~15 raw-offset sites. Move them into a typed port-side struct and
  WIDEN, or key them by the slot index (0..79) as a HANDLE.

### 6. Save data
No pointers. Saves use `objectId`, `mapDataFileId`, positions and flags
(`SaveGameObjectPosition`, `SaveGameCharacterPosition`, `SaveData`). **Keep this
invariant for mods: anything persisted is an ID.**

---

## Where an entity manager would and wouldn't help

**It would:**
- **Long-lived cross-object references** (`targetObj`, `ownerObj`, `parent`,
  `userData` slots holding objects). A generation-checked handle turns a
  use-after-free into a detectable null. That's a real robustness win for mods
  spawning and despawning objects, and it gives spawned objects the stable ID they
  currently lack.
- **Anything persisted or networked:** it already uses `objectId`; a handle table
  generalises that to spawned objects.
- **Overloaded id-or-pointer schemes** (`textureIdxToPtr`, `gameTextDrawBox`'s
  `boxId`): make them id-only.

**It wouldn't:**
- **Byte addresses** (buffers, bitstreams, allocator cursors, DVD reads): a handle
  just resolves to the same pointer you then do arithmetic on. WIDEN.
- **On-disc relocated headers:** the problem is the fixed file layout, not the
  pointer's identity. HOST-STRUCT.
- **The `int obj` convention:** converting ~2,100 sites to handle lookups costs
  at least as much as converting them to `uintptr_t`, and adds a lookup on every
  access in the hottest code (player, baddies). Handles don't make this cheaper.

---

## The decision that dominates: the `int obj` convention

Objects are passed as `obj` / `int obj` / `u8* obj` interchangeably (object.h:11-12).
About 2,100 `(GameObject*)` casts and 212 `(int)obj` casts. Top files:
`dlls/objects/195_Player/player.c` (646), `201_Baddie/Baddie.c` (113), `main/object.c`
(100), engine DLLs 15/11/21 (36-40 each), `433_SH_staff` (35), `main/objlib.c` (32).

Options:

1. **Mechanical widen via the pipeline.** Retype `int obj`-style params/locals that
   carry objects to `uintptr_t` and widen the casts. That's the same rule kinds we
   have, at volume. It needs a scanner that tracks object-carrying ints across a TU
   (`local_int_holds_ptr` + `narrow_param_deref` already find most), probably with
   bulk rule generation rather than hand-authored rules. Biggest effort, fully
   portable.
2. **LOW-4GB object pool.** Allocate `GameObject`s (and their `extra` blocks) below
   4 GB so the `int` round trips stay lossless. Fast to bring up. Not portable
   (macOS, arm64), and every non-object pointer that also passes through those
   ints still breaks. Only as a temporary bring-up path with an exit plan.
3. **Hybrid:** start with (2) for x86-64 bring-up while (1) proceeds DLL by DLL,
   difftested, and retire (2) when the count hits zero.

**Recommendation: (3) if you want something running on x86-64 soon, otherwise
(1).** Either way, decide this before investing in an entity table, because it
determines how object references flow through the code.

---

## Proposed order of work

1. **Decide the `int obj` strategy** (above).
2. **HOST-STRUCT translation for `ModelFileHeader` and `MapBlockData`**: the
   biggest correctness blocker for rendering on 64-bit, and it lands on the
   existing load-time byte-swap seam (`beFix*`, P3). Do pointer translation and
   endian swap in one pass per format.
3. **Textures to id-only:** remove `textureIdxToPtr`'s bit-31 passthrough and the
   in-place `MapBlockData.textures` conversion.
4. **WIDEN the object lists:** `ObjLinkedList`, `objListAdd/remove/walk`, the
   `childObjs` and `gObjList` stride code. Make the `GameObject` offset asserts
   GC-only.
5. **Optional: entity table** with generation-checked handles for the long-lived
   cross-object references, plus IDs for spawned objects (mods, future
   save/network features).
