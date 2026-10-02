/* Driver for the extracted objRender: a parent with three children, two of them
 * staffs (classId 0x2d), each with its own active model bank. Prints which
 * staffs/models staffUpdateSegmentTransforms received, in order. Every object and
 * model comes from mmAlloc (low heap in the oracle, high heap in the mirror). */
#include "shim.h"
#include "snippet.c"
#include <stdio.h>
#include <string.h>

static GameObject *mk(int tag, int classId, int modelTag) {
    GameObject *o = (GameObject *)mmAlloc(sizeof(GameObject), 0, 0);
    ObjModel *m = (ObjModel *)mmAlloc(sizeof(ObjModel), 0, 0);
    memset(o, 0, sizeof *o);
    m->tag = modelTag;
    o->tag = tag; o->anim.classId = classId; o->banks[1] = m; o->bankIndex = 1;
    return o;
}

int main(void)
{
    GameObject *parent = mk(1, 0, 0);
    GameObject *k0 = mk(11, 0x2d, 111), *k1 = mk(22, 0x07, 222), *k2 = mk(33, 0x2d, 333);
    int i;
    parent->childCount = 3;
    parent->childObjs[0] = TO_GUEST(k0);
    parent->childObjs[1] = TO_GUEST(k1);
    parent->childObjs[2] = TO_GUEST(k2);

    objRender(0, 0, 0, 0, parent, 0);   /* flag 0: skip model render, run the child walk */

    printf("calls=%d", gCalls);
    for (i = 0; i < gCalls; i++)
        printf(" staff=%d/model=%d", gSeen[i * 2], gSeen[i * 2 + 1]);
    printf(" rendered=%d\n", (parent->objectFlags & OBJECT_OBJFLAG_RENDERED) != 0);
    return 0;
}
