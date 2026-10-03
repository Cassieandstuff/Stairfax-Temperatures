/* stairfax_model: load a model file into the HOST layout (HOST-STRUCT). See
 * stairfax_model.c and mirror/rules/model_hoststruct.toml. */
#ifndef STAIRFAX_MODEL_H
#define STAIRFAX_MODEL_H
void* stairfax_model_load_unpacked(void* gcBuf, int gcBufSize, int fileOffset, int dataLen, int id);
#endif
