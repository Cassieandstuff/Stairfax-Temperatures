/* Look up every quest state 0..16 the way CFGuardian.c:734/753 do and print the
 * sequence choices found (or "none"). States 11 and 16 have no row.
 *
 * Mirror: the extracted widened table + the retargeted lookup, exactly as the
 * rules rewrite CFGuardian.c. Oracle: the decomp's own int[2] lookup over a
 * 32-bit-layout copy of the table whose pointers are low-memory copies of the
 * arrays, so (int) truncation is lossless, as on the GameCube. */
#include "shim.h"
#include "snippet.c"
#include <stdio.h>
#include <string.h>

#if defined(DIFFTEST_MIRROR)
#  define LOOKUP(t, n, k) seqPairTableLookupPtr((t), (n), (k))
#  define TABLE gCfGuardianSeqStreamTable
static void build_table(void) {}
#else
/* the 15 pointer rows of the decomp table, in order */
static s32* const kRows[15] = {
    gCfGuardianState0Sequences, gCfGuardianState1Sequences, gCfGuardianState2Sequences,
    gCfGuardianState3Sequences, gCfGuardianState4Sequences, gCfGuardianState5Sequences,
    gCfGuardianState6Sequences, gCfGuardianState7Sequences, gCfGuardianState8Sequences,
    gCfGuardianState9Sequences, gCfGuardianState10Sequences, gCfGuardianState12Sequences,
    gCfGuardianState13Sequences, gCfGuardianState14Sequences, gCfGuardianState15Sequences,
};
static const int kKeys[15] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15};
static int (*TABLE)[2];
static void build_table(void)
{
    int i;
    TABLE = (int (*)[2])mmAlloc(CFGUARDIAN_SEQUENCE_TABLE_ROW_COUNT * 8, 0, 0);
    memset(TABLE, 0, CFGUARDIAN_SEQUENCE_TABLE_ROW_COUNT * 8);
    for (i = 0; i < 15; i++) {
        s32* copy = (s32*)mmAlloc(sizeof(s32) * 3, 0, 0);
        memcpy(copy, kRows[i], sizeof(s32) * 3);
        TABLE[i][0] = kKeys[i];
        TABLE[i][1] = (int)(intptr_t)copy;     /* the GameCube's 32-bit pointer */
    }
}
#  define LOOKUP(t, n, k) seqPairTableLookup((t), (n), (k))
#endif

int main(void)
{
    int k;
    build_table();
    for (k = 0; k <= 16; k++) {
        int* choices = (int*)LOOKUP(TABLE, CFGUARDIAN_SEQUENCE_TABLE_ENTRY_COUNT, k);
        if (choices == NULL)
            printf("%d:none ", k);
        else
            printf("%d:%d,%d,%d ", k, choices[0], choices[1], choices[2]);
    }
    printf("\n");
    return 0;
}
