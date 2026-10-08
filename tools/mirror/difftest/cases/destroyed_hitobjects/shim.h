/* Harness support only: GameObject and the object states come from the real
 * headers the snippet includes. The harness allocator is renamed so it doesn't
 * collide with the real mm.h's mmAlloc prototype; the extracted code never
 * calls mmAlloc. Objects are identified by anim.romDefNo. */
#ifndef DESTROYED_SHIM_H
#define DESTROYED_SHIM_H
#define mmAlloc difftest_mmAlloc
#include "difftest.h"
#undef mmAlloc
#include <stdio.h>
#include <string.h>
static char gLog[768];
#define LOG(...) snprintf(gLog + strlen(gLog), sizeof gLog - strlen(gLog), __VA_ARGS__)
#endif
