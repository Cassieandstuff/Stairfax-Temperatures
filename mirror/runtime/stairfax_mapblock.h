/* stairfax_mapblock: load a map block into the HOST layout (HOST-STRUCT). See
 * stairfax_mapblock.c and mirror/rules/mapblock_hoststruct.toml. */
#ifndef STAIRFAX_MAPBLOCK_H
#define STAIRFAX_MAPBLOCK_H
void* stairfax_mapblock_load_unpacked(void* gcBuf, int gcBufSize, int blockOff, int compressedLen);
/* the loaded-texture ID (slot + 1) of a loaded texture, 0 if not loaded; defined in
 * texture.c by mirror/rules/texture_ids.toml (the table is private to texture.c) */
int stairfax_texture_id_of(void* texture);
/* resolves a shader texture slot (an ID) to its texture; declared here because the
 * slot readers rewritten by mapblock_hoststruct.toml don't all include rcp_dolphin_api.h */
void* textureIdxToPtr(int index);
#endif
