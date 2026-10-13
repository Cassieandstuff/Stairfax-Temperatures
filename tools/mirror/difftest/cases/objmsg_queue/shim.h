/* Compile surface for the extracted ObjMsg queue. GameObject carries only what
 * the queue code touches; every object (receivers, senders, pointer params)
 * comes from mmAlloc, so the oracle's (u32) stores are lossless and the
 * mirror's must be full width. */
#ifndef OBJMSG_SHIM_H
#define OBJMSG_SHIM_H
#include "difftest.h"
#include <stddef.h>
#include <stdio.h>
typedef uint32_t u32; typedef int16_t s16; typedef uint8_t u8;
#define STATIC_ASSERT(...)
typedef struct ObjMsgQueue ObjMsgQueue;
typedef struct GameObject {
    struct { s16 romDefNo; s16 classId; } anim;
    ObjMsgQueue* msgQueue;
    int tag;                      /* test identity */
} GameObject;
static int gOverflows;
static void debugPrintf(const char* fmt, ...) { (void)fmt; gOverflows++; }
#endif
