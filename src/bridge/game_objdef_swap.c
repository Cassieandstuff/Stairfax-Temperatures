// game_objdef_swap.c - byte-swap an OBJECTS.bin object definition (ObjDef) from
// big-endian to host order, so the recompiled loadObjectFile relocates its offset
// fields correctly and loadCharacter reads its scalars (dllId/flags/modelFileIds/...)
// as real values. Called from the port fileLoadToBufferOffset right after a def is
// loaded (id MLDF_FILEID_OBJECTS_BIN), before loadObjectFile relocates it.
// See include/main/objanim_internal.h (ObjDef), port/byteswap.h, [[port-object-system]].

#include "main/objanim_internal.h"
#include "port/byteswap.h"

void bswapObjDef(void* p) {
    ObjDef* d = (ObjDef*)p;
    // scalars loadCharacter/objSetupObject read
    BEF32(d->shadowScaleBase); BEF32(d->rootMotionScaleBase);
    BE32(d->flags);
    BE16(d->shadowType); BE16(d->shadowTextureId); BE16(d->hitboxFlags);
    BE16(d->dllId); BE16(d->category);
    BE16(d->primaryCapsuleOffsetA); BE16(d->primaryCapsuleOffsetB);
    BE16(d->secondaryCapsuleOffsetA); BE16(d->secondaryCapsuleOffsetB);
    BE16(d->mapLoadObjectId); BE16(d->npcDialogueTextId);
    for (int i = 0; i < 4; ++i) BE16(d->helpTextIds[i]);
    BE16(d->avoidRadiusX); BE16(d->avoidRadiusZ); BEF32(d->shadowModelScaleBase);
    // the exact offset fields loadObjectFile relocates (must be host order first)
    BE32(d->modelFileIds); BE32(d->textureSlotDefs); BE32(d->jointData);
    BE32(d->extraSetupData); BE32(d->sequenceMap); BE32(d->eventMoveTable);
    BE32(d->hitReactMoveTable); BE32(d->weaponDaTable); BE32(d->attachPoints);
    BE32(d->hitVolumes);
    // modelFileIds[] (offset now host order): swap its modelCount s32 entries
    unsigned mfOff = *(unsigned*)&d->modelFileIds;
    if (mfOff) { int* mf = (int*)((unsigned char*)d + mfOff);
        for (int i = 0; i < d->modelCount; ++i) beFix32(&mf[i]); }
}
