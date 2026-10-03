/* Swap a walker's endpoints (twice, with a phase clamp) and print which node
 * each field points at, by tag. A low-halves-only swap goes unnoticed when both
 * pointers share their high 32 bits (two heap blocks), so on the 64-bit builds
 * the second node lives on the stack, far from the heap. The oracle keeps both in
 * low memory, as on the GameCube. */
#include "shim.h"
#include "snippet.c"
#include <stdio.h>

typedef struct Node { int tag; } Node;

int main(void)
{
    Node* n1 = (Node*)mmAlloc(sizeof(Node), 0, 0);
#if defined(DIFFTEST_ORACLE_LOWMEM)
    Node* n2 = (Node*)mmAlloc(sizeof(Node), 0, 0);
#else
    Node stackNode;
    Node* n2 = &stackNode;
#endif
    RomCurveWalker* w = (RomCurveWalker*)mmAlloc(sizeof(RomCurveWalker), 0, 0);
    n1->tag = 11; n2->tag = 22;
    w->previousNode = TO_GUEST(n1); w->nextNode = TO_GUEST(n2); w->phase = 1.5f;
    RomCurve_swapEndpointNodes(w);
    printf("prev=%d next=%d phase=%.2f", ((Node*)FROM_GUEST(w->previousNode))->tag,
           ((Node*)FROM_GUEST(w->nextNode))->tag, w->phase);
    RomCurve_swapEndpointNodes(w);
    printf(" | prev=%d next=%d\n", ((Node*)FROM_GUEST(w->previousNode))->tag,
           ((Node*)FROM_GUEST(w->nextNode))->tag);
    return 0;
}
