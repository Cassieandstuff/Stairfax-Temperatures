/* Fill a receiver's queue (capacity 3) with messages whose sender is an object
 * and whose param is another object's address or a plain int, overflow it once,
 * then Peek and Pop everything, printing tags (never addresses). Out-parameter
 * storage is pointer-width in the mirror (uintptr_t) and u32 in the oracle, as in
 * the real callers after objmsg.toml. */
#include "shim.h"
#include "snippet.c"

#if defined(DIFFTEST_MIRROR)
typedef uintptr_t OutWord;
#else
typedef u32 OutWord;
#endif

static GameObject* mk(int tag)
{
    GameObject* o = (GameObject*)mmAlloc(sizeof(GameObject), 0, 0);
    o->anim.romDefNo = (s16)tag; o->anim.classId = 0; o->msgQueue = NULL; o->tag = tag;
    return o;
}

int main(void)
{
    GameObject* rx = mk(1);
    GameObject* a = mk(10);
    GameObject* b = mk(20);
    GameObject* payload = mk(99);
    u32 msg; OutWord sender, param;
    int n;

    ObjMsg_AllocQueue(rx, 3);
    ObjMsg_SendToObject(rx, 0x11, a, (uintptr_t)payload);
    ObjMsg_SendToObject(rx, 0x22, b, 7);
    ObjMsg_SendToObject(rx, 0x33, a, (uintptr_t)b);
    n = (int)ObjMsg_SendToObject(rx, 0x44, b, 1);       /* overflow: capacity 3 */
    printf("full=%d overflows=%d", (int)rx->msgQueue->count, gOverflows + (n != 0));

    if (ObjMsg_Peek(rx, &msg, &sender, &param))
        printf(" | peek %x from %d", msg, ((GameObject*)(uintptr_t)sender)->tag);
    while (ObjMsg_Pop(rx, &msg, &sender, &param)) {
        printf(" | pop %x from %d", msg, ((GameObject*)(uintptr_t)sender)->tag);
        if (msg == 0x22) printf(" param=%d", (int)param);
        else printf(" param->%d", ((GameObject*)(uintptr_t)param)->tag);
    }
    printf(" | left=%d\n", (int)rx->msgQueue->count);
    return 0;
}
