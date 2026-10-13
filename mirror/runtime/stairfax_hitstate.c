/* Pins the two views of anim.hitReactState (ObjHitsPriorityState, the
 * allocation type, and ObjHitReactState) to one host layout. See
 * mirror/rules/hitstate.toml. */
#include <stddef.h>
#include "main/objhits_types.h"
#include "main/objHitReact_types.h"

#define SFX_TWIN(a, b) \
    _Static_assert(offsetof(ObjHitReactState, a) == offsetof(ObjHitsPriorityState, b), #a " drifted from " #b)
SFX_TWIN(activeHit, activeHit);
SFX_TWIN(activeEntryByteCount, activeEntryByteCount);
SFX_TWIN(entryBufferByteCapacity, entryBufferByteCapacity);
SFX_TWIN(entries, entries);
SFX_TWIN(resetFrameCount, capsuleScale);
SFX_TWIN(flags, flags);
SFX_TWIN(shapeFlags, shapeFlags);
SFX_TWIN(activeHitboxMode, activeHitboxMode);
SFX_TWIN(resetHitboxMode, resetHitboxMode);
_Static_assert(sizeof(ObjHitReactState) == sizeof(ObjHitsPriorityState), "hit-state views differ in size");
